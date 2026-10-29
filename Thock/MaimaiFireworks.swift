import AppKit
import SpriteKit
import SwiftUI
import OSLog

// Data comes from SDEZ 1.70 FX_GAM_Notes_Center_Back_00, not a generic confetti preset.
struct MaimaiCurve: Decodable {
    let mode: Int
    let max: Double
    let min: Double
    let hi: [[Double]]
    let lo: [[Double]]
    static func hermite(_ keys: [[Double]], _ t: Double) -> Double {
        guard let first = keys.first, let last = keys.last else { return 1 }
        if t <= first[0] { return first[1] }
        if t >= last[0] { return last[1] }
        for j in 1..<keys.count where t <= keys[j][0] {
            let a = keys[j-1], b = keys[j], dt = b[0]-a[0], u = (t-a[0])/dt
            return (2*u*u*u-3*u*u+1)*a[1] + (u*u*u-2*u*u+u)*dt*a[3]
                + (-2*u*u*u+3*u*u)*b[1] + (u*u*u-u*u)*dt*b[2]
        }
        return last[1]
    }
    func value(_ t: Double = 0, random: Double = 0.5) -> Double {
        switch mode {
        case 1: return Self.hermite(hi, t)*max
        case 2: return Self.hermite(lo, t)*min*(1-random) + Self.hermite(hi, t)*max*random
        case 3: return min + (max-min)*random
        default: return max
        }
    }
}

struct MaimaiGradient: Decodable {
    let rgb: [[Double]]
    let alpha: [[Double]]
    func sample(_ t: Double) -> SIMD4<Float> {
        func sampleKeys(_ keys: [[Double]], _ index: Int) -> Float {
            guard let a = keys.first, let z = keys.last else { return 1 }
            if t <= a[0] { return Float(a[index]) }
            for j in 1..<keys.count where t <= keys[j][0] {
                let a = keys[j-1], b = keys[j], f = (t-a[0])/Swift.max(0.000001,b[0]-a[0])
                return Float(a[index]+(b[index]-a[index])*f)
            }
            return Float(z[index])
        }
        return SIMD4(sampleKeys(rgb,1),sampleKeys(rgb,2),sampleKeys(rgb,3),sampleKeys(alpha,1))
    }
}

struct MaimaiEmitter: Decodable {
    let id: Int, name: String, material: Int, textures: [String]
    let delay: Double, duration: Double, rate: Double, bursts: [[Double]]
    let life: MaimaiCurve, size: MaimaiCurve, speed: MaimaiCurve, rotation: MaimaiCurve
    let angularVelocity: MaimaiCurve?, sizeCurve: MaimaiCurve, velocityLimit: MaimaiCurve?
    let colorRandom: Bool, colorHi: MaimaiGradient, colorLo: MaimaiGradient
    let radius: Double, radiusThickness: Double, exposure: Double
}

final class MaimaiEffectData {
    static let shared = MaimaiEffectData()
    let emitters: [MaimaiEmitter]
    var shaders: [Int: SKShader] = [:]
    init() {
        if let url = Bundle.main.url(forResource: "maimai-fireworks", withExtension: "json"),
           let data = try? Data(contentsOf: url), let decoded = try? JSONDecoder().decode([MaimaiEmitter].self, from: data) {
            emitters = decoded
        } else { emitters = []; Logger.audio.error("Maimai effect resource missing") }
        for emitter in emitters {
            let shader: SKShader
            if emitter.material == 49 {
                let url = Bundle.main.url(forResource:"maimai-rainbow",withExtension:"fsh")!
                shader = SKShader(source:(try? String(contentsOf:url,encoding:.utf8)) ?? "")
            } else {
                let texture = SKTexture(imageNamed: emitter.textures[0])
                let ring = emitter.material == 50
                let ringLine = ring ? "rgb *= texture2D(u_second,v_tex_coord).rgb;" : ""
                shader = SKShader(source: """
                void main() {
                    vec4 tex=texture2D(u_first,v_tex_coord);
                    vec3 rgb=tex.rgb;
                    \(ringLine)
                    rgb=rgb*exp2(u_exposure)*a_tint.rgb;
                    float a=tex.a*a_tint.a;
                    float coverage=clamp(max(rgb.r,max(rgb.g,rgb.b))*a,0.,1.);
                    gl_FragColor=vec4(rgb*a,coverage);
                }
                """)
                shader.uniforms = [SKUniform(name:"u_first",texture:texture), SKUniform(name:"u_exposure",float:Float(emitter.exposure))]
                if ring { shader.uniforms.append(SKUniform(name:"u_second",texture:SKTexture(imageNamed:emitter.textures[1]))) }
            }
            shader.attributes = [SKAttribute(name:"a_tint",type:.vectorFloat4),SKAttribute(name:"a_age",type:.float),SKAttribute(name:"a_resolution",type:.vectorFloat2)]
            shaders[emitter.id] = shader
        }
    }
}

final class MaimaiScene: SKScene {
    private var bursts: [SKNode] = []
    var pixelScale: CGFloat = 2
    func burst() {
        // Bound work during fast typing; keep the newest six complete bursts.
        bursts.removeAll { $0.parent == nil }
        if bursts.count >= 6 { bursts.removeFirst().removeFromParent() }
        let root = SKNode(); root.position=CGPoint(x:size.width/2,y:size.height/2)
        // Preserve the circular effect; scale the original 1100-unit diameter to the display.
        root.setScale(max(size.width,size.height)/1000)
        addChild(root); bursts.append(root)
        for emitter in MaimaiEffectData.shared.emitters {
            var times: [Double] = []
            for burst in emitter.bursts { times += Array(repeating:burst[0],count:Int(burst[1])) }
            if emitter.rate > 0 {
                times += (0..<Int((emitter.duration*emitter.rate).rounded())).map { Double($0)/emitter.rate }
            }
            for birth in times {
                let random = Double.random(in:0...1), lifetime=emitter.life.value(random:random)
                let diameter=emitter.size.value(random:Double.random(in:0...1))*100
                let angle=Double.random(in:0..<(2 * .pi)), rotation=emitter.rotation.value(random:Double.random(in:0...1))
                let initialRadius=emitter.radius*(1-emitter.radiusThickness*Double.random(in:0...1))*100
                let speed=emitter.speed.value(random:random)*100
                let spin=emitter.angularVelocity?.value(random:Double.random(in:0...1)) ?? 0
                let node=SKSpriteNode(color:.white,size:CGSize(width:diameter,height:diameter))
                node.shader=MaimaiEffectData.shared.shaders[emitter.id]
                node.blendMode = emitter.material == 49 ? .alpha : (emitter.material == 50 ? .add : .screen)
                node.zPosition = emitter.material == 49 ? 0 : (emitter.material == 50 ? 1 : 2)
                node.setValue(SKAttributeValue(vectorFloat4:SIMD4(0,0,0,0)),forAttribute:"a_tint")
                node.setValue(SKAttributeValue(float:0),forAttribute:"a_age")
                let backing=pixelScale
                node.setValue(SKAttributeValue(vectorFloat2:SIMD2(Float(size.width*backing),Float(size.height*backing))),forAttribute:"a_resolution")
                root.addChild(node)
                var distance=initialRadius, previous=0.0
                let animate=SKAction.customAction(withDuration:lifetime) { sprite, elapsed in
                    let age=Double(elapsed), t=min(1,age/lifetime)
                    let c1=emitter.colorHi.sample(t), c0=emitter.colorLo.sample(t)
                    let color=emitter.colorRandom ? c0+(c1-c0)*Float(random) : c1
                    sprite.setValue(SKAttributeValue(vectorFloat4:color),forAttribute:"a_tint")
                    sprite.setValue(SKAttributeValue(float:Float(age)),forAttribute:"a_age")
                    sprite.setScale(max(0,emitter.sizeCurve.value(t,random:random)))
                    let limit=(emitter.velocityLimit?.value(t,random:random) ?? (speed/100))*100
                    distance += min(speed,max(0,limit))*max(0,age-previous); previous=age
                    sprite.position=CGPoint(x:cos(angle)*distance,y:sin(angle)*distance)
                    sprite.zRotation=rotation+spin*age
                }
                node.run(.sequence([.wait(forDuration:birth+emitter.delay),animate,.removeFromParent()]))
            }
        }
        root.run(.sequence([.wait(forDuration:1.6),.removeFromParent()]))
    }
}

final class MaimaiOverlayWindow: NSWindow {
    override var canBecomeKey: Bool { false }
    override var canBecomeMain: Bool { false }
}

final class MaimaiFireworks {
    static let shared = MaimaiFireworks()
    private var windows: [(MaimaiOverlayWindow,MaimaiMetalView,MaimaiScene)] = []
    private var hideWork: DispatchWorkItem?
    private var previewWindow: NSWindow?
    private var displayLayout = MaimaiFireworks.currentDisplayLayout()
    private(set) var triggerCount = 0
    var visibleOverlayCount: Int { windows.filter { $0.0.isVisible }.count }
    var enabled: Bool {
        get { UserDefaults.standard.object(forKey:"maimaiFireworksEnabled") as? Bool ?? true }
        set {
            UserDefaults.standard.set(newValue,forKey:"maimaiFireworksEnabled")
            if !newValue { clear() }
        }
    }
    var allDisplays: Bool {
        get { UserDefaults.standard.bool(forKey:"maimaiAllDisplays") }
        set { UserDefaults.standard.set(newValue,forKey:"maimaiAllDisplays"); clear() }
    }
    var hdrEnabled: Bool {
        get { UserDefaults.standard.object(forKey:"maimaiHDREnabled") as? Bool ?? false }
        set {
            UserDefaults.standard.set(newValue,forKey:"maimaiHDREnabled")
            for (_,view,_) in windows { view.hdrEnabled=newValue }
        }
    }
    private init() {
        NotificationCenter.default.addObserver(forName:NSApplication.didChangeScreenParametersNotification,object:nil,queue:.main) { [weak self] _ in
            guard let self else { return }
            // EDR headroom also sends this notification. Only changing display
            // geometry requires rebuilding the windows; brightness must not
            // cancel an in-flight burst.
            let layout=Self.currentDisplayLayout()
            if layout != self.displayLayout { self.displayLayout=layout; self.clear() }
        }
        NotificationCenter.default.addObserver(forName:.appStateDidChange,object:nil,queue:.main) { [weak self] _ in
            if !AppEngine.shared.isEnabled() { self?.clear() }
        }
        for name in [NSWorkspace.willSleepNotification,NSWorkspace.screensDidSleepNotification] {
            NSWorkspace.shared.notificationCenter.addObserver(forName:name,object:nil,queue:.main) { [weak self] _ in
                self?.clear()
            }
        }
    }
    private static func currentDisplayLayout() -> [String] {
        NSScreen.screens.map { "\($0.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] ?? 0):\($0.frame):\($0.backingScaleFactor)" }
    }
    // Called only after an actual sound was accepted by the audio queue.
    static func soundAccepted(_ name: String, audible: Bool) {
        guard name == "touch-firework.wav", audible else { return }
        DispatchQueue.main.async {
            guard AppEngine.shared.isEnabled(), shared.enabled,
                  SoundpackManager.shared.getCurrentKeyboardSoundpack()?.brand == "maimai" else { return }
            shared.show()
        }
    }
    func show() {
        dispatchPrecondition(condition:.onQueue(.main))
        guard enabled, !MaimaiEffectData.shared.emitters.isEmpty else { return }
        let pointer=NSEvent.mouseLocation
        let screens=allDisplays ? NSScreen.screens : [NSScreen.screens.first { $0.frame.contains(pointer) } ?? NSScreen.main].compactMap{$0}
        if windows.map({$0.0.frame}) != screens.map({$0.frame}) {
            clear()
            for screen in screens {
                let window=MaimaiOverlayWindow(contentRect:screen.frame,styleMask:.borderless,backing:.buffered,defer:false)
                window.isReleasedWhenClosed=false; window.isOpaque=false; window.backgroundColor = .clear
                window.hasShadow=false; window.ignoresMouseEvents=true
                window.level=NSWindow.Level(rawValue:NSWindow.Level.statusBar.rawValue+1)
                window.collectionBehavior=[.canJoinAllSpaces,.fullScreenAuxiliary,.ignoresCycle,.stationary]
                let scene=MaimaiScene(size:screen.frame.size); scene.backgroundColor = .clear; scene.scaleMode = .resizeFill
                scene.pixelScale=screen.backingScaleFactor
                guard let view=MaimaiMetalView(frame:CGRect(origin:.zero,size:screen.frame.size),scene:scene) else {
                    Logger.audio.error("Maimai Metal renderer unavailable")
                    window.close(); continue
                }
                view.hdrEnabled=hdrEnabled; window.contentView=view
                windows.append((window,view,scene))
            }
        }
        hideWork?.cancel()
        for (window,view,scene) in windows { view.isPaused=false; window.orderFrontRegardless(); scene.burst() }
        triggerCount += 1
        Logger.audio.info("Maimai fireworks burst \(self.triggerCount)")
        let work=DispatchWorkItem { [weak self] in
            for (w,v,_) in self?.windows ?? [] { v.stop(); w.orderOut(nil) }
        }
        hideWork=work; DispatchQueue.main.asyncAfter(deadline:.now()+1.75,execute:work)
    }
    func clear() {
        hideWork?.cancel(); hideWork=nil
        for (window,view,_) in windows { view.stop(); window.close() }
        windows.removeAll()
    }
    // This is an app-owned effect render, never a capture of desktop content.
    func renderedEffectFrame() -> CGImage? {
        return windows.first?.1.captureFrame(headroom:1)?.sdrImage()
    }
    func renderedHDRFrame(headroom:Float? = nil) -> MaimaiHDRFrame? {
        windows.first?.1.captureFrame(headroom:headroom)
    }
    func preview() {
        guard enabled else { return }
        // Preview is available without global keyboard permission and does not change the pack.
        if let url=Bundle.main.url(forResource:"maimai-preview",withExtension:"wav"), let sound=NSSound(contentsOf:url,byReference:false) {
            sound.volume=SettingsEngine.shared.getVolume(for:SettingsEngine.shared.getSelectedAudioDeviceUID() ?? "default")
            previewSound?.stop(); previewSound=sound; sound.play()
        }
        show()
    }
    private var previewSound: NSSound?
    func openPreview() {
        if previewWindow == nil {
            let w=NSWindow(contentRect:NSRect(x:0,y:0,width:460,height:385),styleMask:[.titled,.closable],backing:.buffered,defer:false)
            w.title="Thock maimai · 烟花"; w.isReleasedWhenClosed=false
            w.contentView=NSHostingView(rootView:MaimaiPreviewView()); w.center(); previewWindow=w
        }
        NSApp.activate(ignoringOtherApps:true); previewWindow?.makeKeyAndOrderFront(nil)
    }
}

private struct MaimaiPreviewView: View {
    @AppStorage("maimaiHDREnabled") private var hdrEnabled=false
    var body: some View {
        VStack(alignment:.leading,spacing:18) {
            Text("maimai 烟花").font(.system(size:30,weight:.bold,design:.rounded))
            Text("在桌面上绽放").font(.title3).foregroundStyle(.secondary)
            Button("播放全屏烟花与音效") { MaimaiFireworks.shared.preview() }
                .buttonStyle(.borderedProminent).controlSize(.large)
            Toggle("HDR 峰值高光",isOn:$hdrEnabled)
                .onChange(of:hdrEnabled) { value in MaimaiFireworks.shared.hdrEnabled=value }
            Text("亮芯、星光与光环使用屏幕当前可用的 HDR 峰值。")
                .font(.system(size:12)).foregroundStyle(.secondary)
            Text("混合音效包：按 Tab\n烟花音效包：每次按键\n菜单栏可单独关闭特效、切换显示器范围。")
                .font(.system(size:13)).lineSpacing(5)
            if !AXIsProcessTrusted() {
                Button("打开辅助功能设置，启用全局按键") {
                    NSWorkspace.shared.open(URL(string:"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility")!)
                }.buttonStyle(.link)
            }
        }.padding(30).frame(maxWidth:.infinity,maxHeight:.infinity,alignment:.topLeading)
    }
}
