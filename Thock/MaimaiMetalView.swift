import AppKit
import MetalKit
import SpriteKit
import OSLog

// Preserve the original SpriteKit SDR appearance, then lift only its highlights
// into a transparent, floating-point EDR layer. No desktop pixels are sampled.
final class MaimaiMetalView: MTKView, MTKViewDelegate {
    let effectScene: MaimaiScene
    private let spriteRenderer: SKRenderer
    private let commands: MTLCommandQueue
    private let highlightPipeline: MTLRenderPipelineState
    private var sourceTexture: MTLTexture?
    private var lastTick: CFTimeInterval?
    private var sceneTime: TimeInterval = CACurrentMediaTime()
    private(set) var renderedFrames = 0
    private(set) var lastHeadroom: Float = 1
    var hdrEnabled: Bool = false {
        didSet { configureDynamicRange() }
    }
    override var isOpaque: Bool { false }

    static func headroom(enabled: Bool, available: CGFloat) -> Float {
        guard enabled, available.isFinite else { return 1 }
        return Float(max(1, available))
    }

    init?(frame: CGRect, scene: MaimaiScene) {
        guard let gpu = MTLCreateSystemDefaultDevice(),
              let commands = gpu.makeCommandQueue(),
              let library = gpu.makeDefaultLibrary(),
              let vertex = library.makeFunction(name: "maimaiHDRVertex"),
              let fragment = library.makeFunction(name: "maimaiHDRFragment") else { return nil }
        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.vertexFunction = vertex
        descriptor.fragmentFunction = fragment
        descriptor.colorAttachments[0].pixelFormat = .rgba16Float
        guard let pipeline = try? gpu.makeRenderPipelineState(descriptor: descriptor) else { return nil }
        effectScene = scene
        spriteRenderer = SKRenderer(device: gpu)
        self.commands = commands
        highlightPipeline = pipeline
        super.init(frame: frame, device: gpu)
        colorPixelFormat = .rgba16Float
        colorspace = CGColorSpace(name: CGColorSpace.extendedLinearSRGB)
        clearColor = MTLClearColorMake(0, 0, 0, 0)
        framebufferOnly = true
        preferredFramesPerSecond = 60
        isPaused = true
        enableSetNeedsDisplay = false
        autoResizeDrawable = true
        spriteRenderer.ignoresSiblingOrder = true
        spriteRenderer.scene = scene
        delegate = self
        layer?.isOpaque = false
        configureDynamicRange()
    }

    required init(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    private func configureDynamicRange() {
        guard let metalLayer = layer as? CAMetalLayer else { return }
        metalLayer.wantsExtendedDynamicRangeContent = hdrEnabled
        // The source is authored in sRGB; changing primaries to P3 here would
        // oversaturate it. Extended linear sRGB still carries HDR luminance.
        metalLayer.colorspace = CGColorSpace(name: CGColorSpace.extendedLinearSRGB)
        if #available(macOS 26.0, *) {
            let screen=window?.screen ?? NSScreen.main
            metalLayer.contentsHeadroom = hdrEnabled ? max(1,screen?.maximumPotentialExtendedDynamicRangeColorComponentValue ?? 1) : 1
        }
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        configureDynamicRange()
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        sourceTexture = nil
        effectScene.size = bounds.size
        effectScene.pixelScale = size.width / max(1, bounds.width)
    }

    private func prepareSource(width: Int, height: Int) -> MTLTexture? {
        if sourceTexture?.width != width || sourceTexture?.height != height {
            let descriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm,
                width: width, height: height, mipmapped: false)
            descriptor.usage = [.renderTarget, .shaderRead]
            descriptor.storageMode = .private
            sourceTexture = device?.makeTexture(descriptor: descriptor)
        }
        return sourceTexture
    }

    private func encodeHighlights(source: MTLTexture, destination: MTLTexture,
                                  headroom: Float, command: MTLCommandBuffer) {
        let pass = MTLRenderPassDescriptor()
        pass.colorAttachments[0].texture = destination
        pass.colorAttachments[0].loadAction = .clear
        pass.colorAttachments[0].storeAction = .store
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0)
        guard let encoder = command.makeRenderCommandEncoder(descriptor: pass) else { return }
        var range = headroom
        encoder.setRenderPipelineState(highlightPipeline)
        encoder.setFragmentTexture(source, index: 0)
        encoder.setFragmentBytes(&range, length: MemoryLayout<Float>.size, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        encoder.endEncoding()
    }

    func draw(in view: MTKView) {
        guard let drawable = currentDrawable,
              let source = prepareSource(width: drawable.texture.width, height: drawable.texture.height),
              let command = commands.makeCommandBuffer() else { return }
        effectScene.pixelScale = CGFloat(source.width) / max(1, bounds.width)
        let now=CACurrentMediaTime()
        if let lastTick { sceneTime += min(0.05,max(0,now-lastTick)) }
        lastTick=now
        spriteRenderer.update(atTime: sceneTime)
        let pass = MTLRenderPassDescriptor()
        pass.colorAttachments[0].texture = source
        pass.colorAttachments[0].loadAction = .clear
        pass.colorAttachments[0].storeAction = .store
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0)
        spriteRenderer.render(withViewport: CGRect(x: 0, y: 0, width: source.width, height: source.height),
                              commandBuffer: command, renderPassDescriptor: pass)
        lastHeadroom = Self.headroom(enabled: hdrEnabled,
            available: window?.screen?.maximumExtendedDynamicRangeColorComponentValue ?? 1)
        encodeHighlights(source: source, destination: drawable.texture, headroom: lastHeadroom, command: command)
        command.present(drawable)
        command.addCompletedHandler { buffer in
            if let error = buffer.error { Logger.audio.error("Maimai Metal render failed: \(error)") }
        }
        command.commit()
        renderedFrames += 1
    }

    func stop() {
        isPaused = true
        lastTick = nil
        effectScene.removeAllChildren()
        sourceTexture = nil
    }

    // Test/preview readback of this app's own pixels. Normal playback never
    // waits for the GPU, reads pixels to the CPU, or captures other windows.
    func captureFrame(headroom override: Float? = nil) -> MaimaiHDRFrame? {
        guard let source = sourceTexture, let device, let command = commands.makeCommandBuffer() else { return nil }
        let descriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba16Float,
            width: source.width, height: source.height, mipmapped: false)
        descriptor.usage = .renderTarget
        descriptor.storageMode = .shared
        guard let output = device.makeTexture(descriptor: descriptor) else { return nil }
        let range = override.map { Self.headroom(enabled: true, available: CGFloat($0)) } ?? lastHeadroom
        encodeHighlights(source: source, destination: output, headroom: range, command: command)
        command.commit()
        command.waitUntilCompleted()
        guard command.status == .completed else { return nil }
        var bits = [UInt16](repeating: 0, count: output.width * output.height * 4)
        bits.withUnsafeMutableBytes { bytes in
            output.getBytes(bytes.baseAddress!, bytesPerRow: output.width * 8,
                from: MTLRegionMake2D(0, 0, output.width, output.height), mipmapLevel: 0)
        }
        return MaimaiHDRFrame(width: output.width, height: output.height, components: bits, headroom: range)
    }
}

struct MaimaiHDRFrame {
    let width: Int, height: Int
    let components: [UInt16]
    let headroom: Float

    var peak: Float {
        stride(from: 0, to: components.count, by: 4).reduce(Float(0)) { peak, i in
            max(peak, Float(Float16(bitPattern: components[i])),
                Float(Float16(bitPattern: components[i+1])), Float(Float16(bitPattern: components[i+2])))
        }
    }

    // An SDR preview preserves the shape/color but cannot demonstrate physical
    // XDR brightness. Keep HDR validation on the floating-point pixels above.
    func sdrImage() -> CGImage? {
        var bytes = [UInt8](repeating: 0, count: components.count)
        for i in stride(from: 0, to: components.count, by: 4) {
            let a = max(0, min(1, Float(Float16(bitPattern: components[i+3]))))
            guard a > 0 else { continue }
            for c in 0..<3 {
                let linear = max(0, min(1, Float(Float16(bitPattern: components[i+c])) / a))
                let srgb = linear <= 0.0031308 ? linear * 12.92 : 1.055 * pow(linear, 1/2.4) - 0.055
                bytes[i+c] = UInt8(max(0, min(255, (srgb * a * 255).rounded())))
            }
            bytes[i+3] = UInt8((a * 255).rounded())
        }
        guard let provider = CGDataProvider(data: Data(bytes) as CFData) else { return nil }
        return CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
            bytesPerRow: width * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
            provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
    }
}
