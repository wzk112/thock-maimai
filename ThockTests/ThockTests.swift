//
//  ThockTests.swift
//  ThockTests
//
//  Created by Kamil Łobiński on 07/03/2025.
//

import Testing
import Foundation
import AppKit
import AVFoundation
@testable import Thock_maimai

@Suite(.serialized) struct ThockTests {

    // Simulates a busy UI/render pass without blocking the Core Audio thread.
    private func blockUI(for seconds: TimeInterval) { Thread.sleep(forTimeInterval: seconds) }

    @Test @MainActor func audioContinuesWhileMainThreadIsBusy() async throws {
        let mixed = try #require(SoundpackDatabase().getSoundpack(by:
            UUID(uuidString: "354BD030-0A95-57B5-ABE4-5E1AB05CADEC")!))
        let manager = SoundManager.shared
        manager.preloadSounds(for: mixed)
        manager.play(sound: "touch.wav")
        try await Task.sleep(for: .milliseconds(100))
        let before = manager.audioDiagnostics()
        blockUI(for: 0.3)
        let after = manager.audioDiagnostics()
        print("Audio UI-stall regression: buffers=\(after.buffers-before.buffers), sourceFrames=\(after.sourceFramesConsumed-before.sourceFramesConsumed), callbackOnMain=\(after.callbackOnMainThread)")
        #expect(after.buffers - before.buffers > 20)
        #expect(after.sourceFramesConsumed - before.sourceFramesConsumed > 8_000)
        #expect(!after.callbackOnMainThread)
    }

    @Test @MainActor func rapidAudioIsBoundedAndResumesAfterIdle() async throws {
        let manager = SoundManager.shared
        let mixed = try #require(SoundpackDatabase().getSoundpack(by:
            UUID(uuidString: "354BD030-0A95-57B5-ABE4-5E1AB05CADEC")!))
        manager.preloadSounds(for: mixed)
        let oldVolume = manager.setTestVolume(0)
        defer { _ = manager.setTestVolume(oldVolume) }
        for _ in 0..<500 { manager.play(sound: "tap.wav") }
        #expect(manager.audioDiagnostics().activeVoices <= SoundManager.maximumActiveSounds)
        try await Task.sleep(for: .milliseconds(1200))
        #expect(manager.audioDiagnostics().activeVoices == 0)
        for _ in 0..<3 {
            manager.stopIfIdleForTesting()
            #expect(!manager.audioDiagnostics().running)
            let before = manager.audioDiagnostics().buffers
            manager.play(sound: "answer.wav")
            try await Task.sleep(for: .milliseconds(500))
            #expect(manager.audioDiagnostics().running)
            #expect(manager.audioDiagnostics().buffers > before + 20)
            #expect(manager.audioDiagnostics().activeVoices == 0)
        }
    }

    @Test @MainActor func queueRebuildDoesNotRacePlayback() async throws {
        let manager = SoundManager.shared
        let oldVolume = manager.setTestVolume(0)
        defer { _ = manager.setTestVolume(oldVolume) }
        await withCheckedContinuation { continuation in
            DispatchQueue.global(qos: .userInitiated).async {
                DispatchQueue.concurrentPerform(iterations: 16) { index in
                    if index % 8 == 0 { manager.reinitializeAfterWake() }
                    else { manager.play(sound: "tap.wav") }
                }
                continuation.resume()
            }
        }
        let before = manager.audioDiagnostics().buffers
        try await Task.sleep(for: .milliseconds(150))
        #expect(manager.audioDiagnostics().ready)
        #expect(manager.audioDiagnostics().buffers > before + 10)
    }

    @Test func customPackSampleRatePreservesDuration() throws {
        let format = try #require(AVAudioFormat(standardFormatWithSampleRate: 44100, channels: 2))
        let input = try #require(AVAudioPCMBuffer(pcmFormat: format, frameCapacity: 4410))
        input.frameLength = 4410
        for channel in 0..<2 {
            for frame in 0..<4410 { input.floatChannelData![channel][frame] = sin(Float(frame) * 0.1) * 0.1 }
        }
        let converted = try #require(SoundManager.shared.resampleIfNeeded(input))
        #expect(converted.format.sampleRate == 48000)
        #expect(abs(Int(converted.frameLength) - 4800) <= 1)
        #expect(converted.floatChannelData![0][100].isFinite)
    }

    @Test @MainActor func hdrDefaultsOffOnFreshPreferences() {
        let saved=UserDefaults.standard.object(forKey:"maimaiHDREnabled")
        defer {
            if let saved { UserDefaults.standard.set(saved,forKey:"maimaiHDREnabled") }
            else { UserDefaults.standard.removeObject(forKey:"maimaiHDREnabled") }
        }
        UserDefaults.standard.removeObject(forKey:"maimaiHDREnabled")
        #expect(!MaimaiFireworks.shared.hdrEnabled)
    }

    @Test func originalFadeCurveAndResources() throws {
        let emitters = MaimaiEffectData.shared.emitters
        #expect(emitters.count == 6)
        let star = try #require(emitters.first { $0.id == 47717 })
        #expect(abs(star.sizeCurve.value(0.5) - 0.25) < 0.000001)
        #expect(star.sizeCurve.value(1) == 0)
        #expect(abs(star.delay - 0.02) < 0.00001)
        #expect(abs(star.life.value() - 0.5) < 0.00001)
        #expect(abs(star.colorHi.sample(1).w) < 0.00001)
        #expect(emitters.first { $0.id == 47710 }?.material == 49)
        #expect(MaimaiEffectData.shared.shaders.count == 6)
    }

    @Test @MainActor func ordinarySoundsNeverTrigger() async throws {
        let previous = MaimaiFireworks.shared.triggerCount
        MaimaiFireworks.soundAccepted("tap.wav", audible: true)
        MaimaiFireworks.soundAccepted("touch-firework.wav", audible: false)
        try await Task.sleep(for: .milliseconds(80))
        #expect(MaimaiFireworks.shared.triggerCount == previous)
    }

    @Test @MainActor func renderTransparentEffectAndCleanup() async throws {
        let fx=MaimaiFireworks.shared, old=fx.enabled
        defer { fx.clear(); fx.enabled=old }
        fx.enabled=true
        fx.show()
        try await Task.sleep(for:.milliseconds(180))
        let cg=try #require(fx.renderedEffectFrame())
        let bitmap=NSBitmapImageRep(cgImage:cg)
        let url=FileManager.default.temporaryDirectory.appendingPathComponent("maimai-effect-check.png")
        try bitmap.representation(using:.png,properties:[:])?.write(to:url)
        #expect(cg.width > 100)
        #expect((bitmap.colorAt(x:0,y:0)?.alphaComponent ?? 1) < 0.01)
        var visible=0
        for y in stride(from:0,to:bitmap.pixelsHigh,by:20) {
            for x in stride(from:0,to:bitmap.pixelsWide,by:20) {
                if (bitmap.colorAt(x:x,y:y)?.alphaComponent ?? 0) > 0.05 { visible += 1 }
            }
        }
        #expect(visible > 20)
        try await Task.sleep(for:.milliseconds(1800))
        #expect(fx.visibleOverlayCount == 0)
    }

    @Test @MainActor func offSwitchSuppressesEffect() {
        let fx=MaimaiFireworks.shared, old=fx.enabled, previous=fx.triggerCount
        defer { fx.enabled=old }
        fx.enabled=false
        fx.show()
        #expect(fx.triggerCount == previous)
    }

    @Test @MainActor func actualSoundpackRouteAndMute() async throws {
        let fx=MaimaiFireworks.shared, oldEffect=fx.enabled, oldEnabled=AppEngine.shared.isEnabled()
        let oldPack=SoundpackManager.shared.getCurrentKeyboardSoundpack()
        let mixed=try #require(SoundpackDatabase().getSoundpack(by:UUID(uuidString:"354BD030-0A95-57B5-ABE4-5E1AB05CADEC")!))
        let onlyFire=try #require(SoundpackDatabase().getSoundpack(by:UUID(uuidString:"A039B57E-0322-5204-94DA-6B5A4DB76F97")!))
        defer {
            fx.clear(); fx.enabled=oldEffect; AppEngine.shared.setEnabled(oldEnabled)
            if let oldPack { SoundpackEngine.shared.applyKeyboard(soundpack:oldPack) }
        }
        fx.enabled=true; AppEngine.shared.setEnabled(true)
        SoundpackEngine.shared.applyKeyboard(soundpack:mixed)
        let initial=fx.triggerCount
        SoundEngine.shared.play(for:0,isKeyDown:true)
        try await Task.sleep(for:.milliseconds(80))
        #expect(fx.triggerCount == initial)
        SoundEngine.shared.play(for:48,isKeyDown:true)
        try await Task.sleep(for:.milliseconds(80))
        #expect(fx.triggerCount == initial+1)
        SoundEngine.shared.play(for:48,isKeyDown:false)
        AppEngine.shared.setEnabled(false)
        SoundEngine.shared.play(for:48,isKeyDown:true)
        try await Task.sleep(for:.milliseconds(80))
        #expect(fx.triggerCount == initial+1)
        #expect(fx.visibleOverlayCount == 0)
        AppEngine.shared.setEnabled(true)
        SoundpackEngine.shared.applyKeyboard(soundpack:onlyFire)
        SoundEngine.shared.play(for:0,isKeyDown:true)
        try await Task.sleep(for:.milliseconds(80))
        #expect(fx.triggerCount == initial+2)
    }

    @Test @MainActor func rapidBurstsAreBounded() {
        let scene=MaimaiScene(size:CGSize(width:1920,height:1080))
        for _ in 0..<30 { scene.burst() }
        #expect(scene.children.count == 6)
    }

    @Test func hdrHeadroomRespectsDisplayAndSwitch() {
        #expect(MaimaiMetalView.headroom(enabled:false,available:8) == 1)
        #expect(MaimaiMetalView.headroom(enabled:true,available:1) == 1)
        #expect(MaimaiMetalView.headroom(enabled:true,available:1.75) == 1.75)
        #expect(MaimaiMetalView.headroom(enabled:true,available:16) == 16)
        #expect(MaimaiMetalView.headroom(enabled:true,available:.nan) == 1)
    }

    @Test @MainActor func hdrPixelsExceedSDRAndPreserveTransparency() async throws {
        let fx=MaimaiFireworks.shared, oldEnabled=fx.enabled, oldHDR=fx.hdrEnabled
        defer { fx.clear(); fx.enabled=oldEnabled; fx.hdrEnabled=oldHDR }
        fx.enabled=true; fx.hdrEnabled=true; fx.show()
        try await Task.sleep(for:.milliseconds(220))
        let hdr=try #require(fx.renderedHDRFrame(headroom:16))
        let sdr=try #require(fx.renderedHDRFrame(headroom:1))
        let actual=try #require(fx.renderedHDRFrame())
        fx.hdrEnabled=false
        try await Task.sleep(for:.milliseconds(80))
        let disabled=try #require(fx.renderedHDRFrame())
        let hdrPeak=hdr.peak, sdrPeak=sdr.peak
        #expect(hdrPeak > 15.9)
        #expect(hdrPeak <= 16.01)
        #expect(sdrPeak <= 1.001)
        #expect(hdr.components[3] == 0)
        #expect(hdr.components[0] == 0 && hdr.components[1] == 0 && hdr.components[2] == 0)
        #expect(hdr.components.count == sdr.components.count)
        for i in stride(from:0,to:hdr.components.count,by:4*128) {
            #expect(hdr.components[i+3] == sdr.components[i+3])
            for c in 0..<4 { #expect(Float16(bitPattern:hdr.components[i+c]).isFinite) }
        }
        #expect(disabled.headroom == 1)
        #expect(disabled.peak <= 1.001)
        let evidence:[String:Any]=[
            "renderTarget":"RGBA16Float", "colorSpace":"extendedLinearSRGB",
            "controlledHDRHeadroom":hdr.headroom, "controlledHDRPeak":hdrPeak,
            "SDRPeak":sdrPeak, "liveDisplayHeadroom":actual.headroom,
            "liveDisplayPeak":actual.peak, "HDRDisabledHeadroom":disabled.headroom,
            "potentialDisplayHeadroom":NSScreen.main?.maximumPotentialExtendedDynamicRangeColorComponentValue ?? 1,
            "transparencyPreserved":true,
            "note":"GPU pixel values; physical brightness and subjective appearance require the XDR display."
        ]
        try JSONSerialization.data(withJSONObject:evidence,options:[.prettyPrinted,.sortedKeys]).write(to:
            FileManager.default.temporaryDirectory.appendingPathComponent("maimai-hdr-check.json"))
    }

    @Test @MainActor func burstResumesAfterIdleAndHeadroomNotification() async throws {
        let fx=MaimaiFireworks.shared, old=fx.enabled
        defer { fx.clear(); fx.enabled=old }
        fx.enabled=true; fx.show()
        try await Task.sleep(for:.milliseconds(1850))
        #expect(fx.visibleOverlayCount == 0)
        fx.show()
        NotificationCenter.default.post(name:NSApplication.didChangeScreenParametersNotification,object:nil)
        try await Task.sleep(for:.milliseconds(200))
        #expect(fx.visibleOverlayCount > 0)
        let frame=try #require(fx.renderedHDRFrame(headroom:1))
        #expect(frame.peak > 0.1)
    }

}
