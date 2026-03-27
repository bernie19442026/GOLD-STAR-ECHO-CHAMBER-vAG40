#import "standalone_ui.h"
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <AVFoundation/AVFoundation.h>
#include "../audio/parameters.h"
#include <string>
#include <cmath>
#include <mach-o/dyld.h>

// Forward declare
@interface GoldStarAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property (nonatomic) StandaloneApp* app;
@property (nonatomic, strong) NSWindow* window;
@property (nonatomic, strong) NSStatusItem* statusItem;
@end

// GUI image dimensions
static const CGFloat kImageW = 1376.0;
static const CGFloat kImageH = 768.0;

// ============================================================================
// RotaryKnobOverlay — transparent draggable knob over the GUI image
// Draws only the indicator line; the knob body is in the background image.
// ============================================================================
@interface RotaryKnobOverlay : NSView
@property (nonatomic) double minValue, maxValue, value;
@property (nonatomic, copy) void (^onChange)(double value);
@property (nonatomic, copy) NSString* label;
@end

@implementation RotaryKnobOverlay {
    NSPoint _dragStart;
    double _dragStartValue;
    BOOL _isDragging;
}

- (instancetype)initWithFrame:(NSRect)frame
                          min:(double)minVal max:(double)maxVal value:(double)val
                        label:(NSString*)lbl {
    self = [super initWithFrame:frame];
    if (self) {
        _minValue = minVal; _maxValue = maxVal; _value = val;
        _label = lbl; _isDragging = NO;
    }
    return self;
}

- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }
- (BOOL)isOpaque { return NO; }

- (void)drawRect:(NSRect)dirtyRect {
    // Transparent background — image shows through
    NSRect bounds = self.bounds;
    CGFloat cx = NSMidX(bounds);
    CGFloat cy = NSMidY(bounds);
    CGFloat radius = MIN(bounds.size.width, bounds.size.height) * 0.42;

    double normalized = (_value - _minValue) / (_maxValue - _minValue);
    double angleDeg = -135.0 + normalized * 270.0;
    double angleRad = (angleDeg - 90.0) * M_PI / 180.0;

    CGFloat innerR = radius * 0.15;
    CGFloat outerR = radius * 0.92;
    NSPoint inner = NSMakePoint(cx + innerR * cos(angleRad), cy - innerR * sin(angleRad));
    NSPoint outer = NSMakePoint(cx + outerR * cos(angleRad), cy - outerR * sin(angleRad));

    NSBezierPath* line = [NSBezierPath bezierPath];
    [line moveToPoint:inner];
    [line lineToPoint:outer];
    [line setLineWidth:_isDragging ? 3.0 : 2.5];

    NSColor* lineColor = _isDragging ?
        [NSColor colorWithRed:1.0 green:0.9 blue:0.5 alpha:0.95] :
        [NSColor colorWithRed:0.914 green:0.757 blue:0.463 alpha:0.85];
    [lineColor setStroke];
    [line stroke];

    // Glow ring when dragging
    if (_isDragging) {
        NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:NSInsetRect(bounds, 4, 4)];
        [[NSColor colorWithRed:1.0 green:0.9 blue:0.5 alpha:0.15] setStroke];
        [ring setLineWidth:2.0];
        [ring stroke];
    }
}

- (void)mouseDown:(NSEvent *)event {
    _isDragging = YES;
    _dragStart = [NSEvent mouseLocation];
    _dragStartValue = _value;
    [self setNeedsDisplay:YES];

    while (YES) {
        NSEvent* nextEvent = [self.window nextEventMatchingMask:
            NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp];
        if (nextEvent.type == NSEventTypeLeftMouseUp) {
            _isDragging = NO;
            [self setNeedsDisplay:YES];
            break;
        }
        NSPoint current = [NSEvent mouseLocation];
        double deltaY = current.y - _dragStart.y;
        double sensitivity = (_maxValue - _minValue) / 250.0;
        double newValue = _dragStartValue + deltaY * sensitivity;
        _value = fmax(_minValue, fmin(_maxValue, newValue));
        if (_onChange) _onChange(_value);
        [self setNeedsDisplay:YES];
    }
}

@end

// ============================================================================
// FaderOverlay — transparent draggable fader over the GUI image
// Draws only the handle position marker.
// ============================================================================
@interface FaderOverlay : NSView
@property (nonatomic) double minValue, maxValue, value;
@property (nonatomic, copy) void (^onChange)(double value);
@property (nonatomic) BOOL isRedHandle;
@end

@implementation FaderOverlay {
    BOOL _isDragging;
    NSPoint _dragStart;
    double _dragStartValue;
}

- (instancetype)initWithFrame:(NSRect)frame
                          min:(double)minVal max:(double)maxVal value:(double)val
                    redHandle:(BOOL)red {
    self = [super initWithFrame:frame];
    if (self) {
        _minValue = minVal; _maxValue = maxVal; _value = val;
        _isRedHandle = red; _isDragging = NO;
    }
    return self;
}

- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }
- (BOOL)isOpaque { return NO; }

- (void)drawRect:(NSRect)dirtyRect {
    NSRect bounds = self.bounds;
    CGFloat margin = 8.0;
    CGFloat trackH = bounds.size.height - margin * 2;

    double normalized = (_value - _minValue) / (_maxValue - _minValue);
    CGFloat handleY = margin + normalized * trackH;
    CGFloat handleW = bounds.size.width - 4;
    CGFloat handleH = 12.0;
    NSRect handleRect = NSMakeRect(2, handleY - handleH/2, handleW, handleH);

    // Semi-transparent handle overlay (the image shows the real track)
    NSColor* handleColor = _isRedHandle ?
        [NSColor colorWithRed:0.8 green:0.1 blue:0.1 alpha:0.7] :
        [NSColor colorWithRed:0.85 green:0.80 blue:0.73 alpha:0.6];

    if (_isDragging) {
        handleColor = _isRedHandle ?
            [NSColor colorWithRed:1.0 green:0.2 blue:0.2 alpha:0.9] :
            [NSColor colorWithRed:1.0 green:0.95 blue:0.85 alpha:0.8];
    }

    [handleColor setFill];
    NSBezierPath* handle = [NSBezierPath bezierPathWithRoundedRect:handleRect xRadius:2 yRadius:2];
    [handle fill];

    // Thin bright line on handle center
    NSBezierPath* centerLine = [NSBezierPath bezierPath];
    [centerLine moveToPoint:NSMakePoint(4, handleY)];
    [centerLine lineToPoint:NSMakePoint(handleW, handleY)];
    [centerLine setLineWidth:1.0];
    [[NSColor colorWithWhite:1.0 alpha:0.4] setStroke];
    [centerLine stroke];
}

- (void)mouseDown:(NSEvent *)event {
    _isDragging = YES;
    _dragStart = [NSEvent mouseLocation];
    _dragStartValue = _value;
    [self setNeedsDisplay:YES];

    while (YES) {
        NSEvent* nextEvent = [self.window nextEventMatchingMask:
            NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp];
        if (nextEvent.type == NSEventTypeLeftMouseUp) {
            _isDragging = NO;
            [self setNeedsDisplay:YES];
            break;
        }
        NSPoint current = [NSEvent mouseLocation];
        double deltaY = current.y - _dragStart.y;
        double sensitivity = (_maxValue - _minValue) / 180.0;
        double newValue = _dragStartValue + deltaY * sensitivity;
        _value = fmax(_minValue, fmin(_maxValue, newValue));
        if (_onChange) _onChange(_value);
        [self setNeedsDisplay:YES];
    }
}

@end

// ============================================================================
// VUMeterOverlay — draws animated needle over the VU meter in the image
// ============================================================================
@interface VUMeterOverlay : NSView
@property (nonatomic) float level;
@property (nonatomic) float smoothedLevel;
@end

@implementation VUMeterOverlay

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) { _level = 0; _smoothedLevel = 0; }
    return self;
}

- (BOOL)isOpaque { return NO; }

- (void)drawRect:(NSRect)dirtyRect {
    NSRect bounds = self.bounds;

    // Smooth the needle
    _smoothedLevel += 0.15f * (_level - _smoothedLevel);
    float normalizedLevel = fmin(1.0f, fmax(0.0f, _smoothedLevel));

    // Needle pivots from bottom center
    CGFloat cx = NSMidX(bounds);
    CGFloat cy = 6.0;
    CGFloat needleLen = bounds.size.height * 0.82;

    // Map 0-1 to -40deg to +40deg
    float angleDeg = -40.0f + normalizedLevel * 80.0f;
    float angleRad = angleDeg * M_PI / 180.0f;

    NSPoint tip = NSMakePoint(cx + needleLen * sinf(angleRad),
                               cy + needleLen * cosf(angleRad));

    NSBezierPath* needle = [NSBezierPath bezierPath];
    [needle moveToPoint:NSMakePoint(cx, cy)];
    [needle lineToPoint:tip];
    [needle setLineWidth:1.2];
    [[NSColor colorWithRed:0.1 green:0.1 blue:0.1 alpha:0.9] setStroke];
    [needle stroke];

    // Needle pivot dot
    NSRect pivotRect = NSMakeRect(cx - 3, cy - 3, 6, 6);
    [[NSColor colorWithRed:0.2 green:0.2 blue:0.2 alpha:0.8] setFill];
    [[NSBezierPath bezierPathWithOvalInRect:pivotRect] fill];
}

@end

// ============================================================================
// IRButtonOverlay — transparent clickable overlay for IR selector buttons
// ============================================================================
@interface IRButtonOverlay : NSView
@property (nonatomic) BOOL isSelected;
@property (nonatomic, copy) void (^onSelect)(void);
@end

@implementation IRButtonOverlay

- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }
- (BOOL)isOpaque { return NO; }

- (void)drawRect:(NSRect)dirtyRect {
    if (_isSelected) {
        // Highlight ring around selected button
        NSRect bounds = NSInsetRect(self.bounds, 2, 2);
        NSBezierPath* ring = [NSBezierPath bezierPathWithRoundedRect:bounds xRadius:4 yRadius:4];
        [[NSColor colorWithRed:0.914 green:0.757 blue:0.463 alpha:0.5] setStroke];
        [ring setLineWidth:2.5];
        [ring stroke];
    }
}

- (void)mouseDown:(NSEvent *)event {
    if (_onSelect) _onSelect();
}

@end

// ============================================================================
// ToggleOverlay — transparent toggle switch overlay
// ============================================================================
@interface ToggleOverlay : NSView
@property (nonatomic) BOOL isOn;
@property (nonatomic, copy) void (^onChange)(BOOL isOn);
@end

@implementation ToggleOverlay

- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }
- (BOOL)isOpaque { return NO; }

- (void)drawRect:(NSRect)dirtyRect {
    if (_isOn) {
        // Green glow when ON
        NSRect bounds = NSInsetRect(self.bounds, 1, 1);
        NSBezierPath* glow = [NSBezierPath bezierPathWithRoundedRect:bounds xRadius:4 yRadius:4];
        [[NSColor colorWithRed:0.2 green:0.8 blue:0.3 alpha:0.3] setFill];
        [glow fill];
    }
}

- (void)mouseDown:(NSEvent *)event {
    _isOn = !_isOn;
    if (_onChange) _onChange(_isOn);
    [self setNeedsDisplay:YES];
}

@end

// ============================================================================
// TransportButtonOverlay — transparent clickable transport button
// ============================================================================
@interface TransportButtonOverlay : NSView
@property (nonatomic, copy) void (^onPress)(void);
@end

@implementation TransportButtonOverlay

- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }
- (BOOL)isOpaque { return NO; }

- (void)drawRect:(NSRect)dirtyRect {
    // Fully transparent — button graphics are in the image
}

- (void)mouseDown:(NSEvent *)event {
    // Brief flash feedback
    self.layer.backgroundColor = [NSColor colorWithWhite:1.0 alpha:0.15].CGColor;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC),
        dispatch_get_main_queue(), ^{
            self.layer.backgroundColor = [NSColor clearColor].CGColor;
        });
    if (_onPress) _onPress();
}

@end

// ============================================================================
// BypassButtonOverlay — toggle bypass button
// ============================================================================
@interface BypassButtonOverlay : NSView
@property (nonatomic) BOOL isOn;
@property (nonatomic, copy) void (^onChange)(BOOL isOn);
@end

@implementation BypassButtonOverlay

- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }
- (BOOL)isOpaque { return NO; }

- (void)drawRect:(NSRect)dirtyRect {
    if (_isOn) {
        // Red glow when bypassed
        NSRect bounds = NSInsetRect(self.bounds, 1, 1);
        NSBezierPath* bg = [NSBezierPath bezierPathWithRoundedRect:bounds xRadius:3 yRadius:3];
        [[NSColor colorWithRed:0.7 green:0.1 blue:0.1 alpha:0.5] setFill];
        [bg fill];

        // "BYPASSED" text overlay
        NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
        style.alignment = NSTextAlignmentCenter;
        NSDictionary* attrs = @{
            NSFontAttributeName: [NSFont boldSystemFontOfSize:9.0],
            NSForegroundColorAttributeName: [NSColor colorWithRed:1.0 green:0.3 blue:0.3 alpha:0.9],
            NSParagraphStyleAttributeName: style
        };
        [@"BYPASSED" drawInRect:NSMakeRect(0, (self.bounds.size.height - 12)/2,
            self.bounds.size.width, 14) withAttributes:attrs];
    }
}

- (void)mouseDown:(NSEvent *)event {
    _isOn = !_isOn;
    if (_onChange) _onChange(_isOn);
    [self setNeedsDisplay:YES];
}

@end

// ============================================================================
// Helper: Convert image coords (origin top-left) to NSView coords (origin bottom-left)
// ============================================================================
static NSRect imageRectToView(CGFloat imgX, CGFloat imgY, CGFloat w, CGFloat h) {
    // imgX, imgY = center in image coords (top-left origin)
    // Convert to bottom-left origin for NSView
    CGFloat viewX = imgX - w / 2.0;
    CGFloat viewY = kImageH - imgY - h / 2.0;
    return NSMakeRect(viewX, viewY, w, h);
}

// ============================================================================
// Main App Delegate
// ============================================================================
@implementation GoldStarAppDelegate {
    NSTimer* _meterTimer;
    VUMeterOverlay* _vuInput;
    VUMeterOverlay* _vuProcess;
    VUMeterOverlay* _vuOutput;
    NSMutableArray<IRButtonOverlay*>* _irButtons;
    ToggleOverlay* _pendingMicToggle;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    // Window sized to the GUI image
    _window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(50, 50, kImageW, kImageH)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
                    backing:NSBackingStoreBuffered defer:NO];
    [_window setTitle:@"Gold Star Echo Chamber vAG40"];
    [_window setDelegate:self];

    NSView* content = [_window contentView];
    content.wantsLayer = YES;

    // === Background image ===
    NSString* imgPath = [self findGUIImage];
    if (imgPath) {
        NSImage* bgImage = [[NSImage alloc] initWithContentsOfFile:imgPath];
        if (bgImage) {
            NSImageView* bgView = [[NSImageView alloc] initWithFrame:
                NSMakeRect(0, 0, kImageW, kImageH)];
            bgView.image = bgImage;
            bgView.imageScaling = NSImageScaleAxesIndependently;
            [content addSubview:bgView];
        }
    }

    // ================================================================
    // CONTROL OVERLAYS — mapped to exact pixel positions on the image
    // All coordinates are (centerX, centerY) in image space (top-left origin)
    // ================================================================

    // --- INPUT CONTROL: 3 knobs ---
    CGFloat smallKnob = 80.0;

    RotaryKnobOverlay* lpKnob = [[RotaryKnobOverlay alloc]
        initWithFrame:imageRectToView(171, 363, smallKnob, smallKnob)
                  min:200 max:20000 value:20000 label:@"LOW PASS"];
    lpKnob.onChange = ^(double v) { self.app->setParameter(ParameterID::LOW_PASS, (float)v); };
    [content addSubview:lpKnob];

    RotaryKnobOverlay* hpKnob = [[RotaryKnobOverlay alloc]
        initWithFrame:imageRectToView(281, 363, smallKnob, smallKnob)
                  min:20 max:8000 value:20 label:@"HIGH PASS"];
    hpKnob.onChange = ^(double v) { self.app->setParameter(ParameterID::HIGH_PASS, (float)v); };
    [content addSubview:hpKnob];

    RotaryKnobOverlay* gainKnob = [[RotaryKnobOverlay alloc]
        initWithFrame:imageRectToView(393, 358, smallKnob + 10, smallKnob + 10)
                  min:-80 max:12 value:0 label:@"GAIN"];
    gainKnob.onChange = ^(double v) { self.app->setParameter(ParameterID::INPUT_GAIN, (float)v); };
    [content addSubview:gainKnob];

    // --- FIVE-BAND INPUT EQ: 5 faders ---
    CGFloat eqFaderW = 30.0;
    CGFloat eqFaderH = 300.0;
    CGFloat eqFaderXs[] = {170, 225, 279, 335, 389};
    CGFloat eqFaderCenterY = 590.0;  // center of the fader track area
    ParameterID eqParams[] = {ParameterID::EQ_100, ParameterID::EQ_250,
                               ParameterID::EQ_1K, ParameterID::EQ_4K, ParameterID::EQ_10K};

    for (int i = 0; i < 5; i++) {
        FaderOverlay* fader = [[FaderOverlay alloc]
            initWithFrame:imageRectToView(eqFaderXs[i], eqFaderCenterY, eqFaderW, eqFaderH)
                      min:-12 max:12 value:0 redHandle:NO];
        ParameterID pid = eqParams[i];
        fader.onChange = ^(double v) { self.app->setParameter(pid, (float)v); };
        [content addSubview:fader];
    }

    // --- IR MODULES: 5 buttons (A-E) ---
    _irButtons = [NSMutableArray array];
    CGFloat irBtnXs[] = {506, 596, 683, 770, 856};
    CGFloat irBtnY = 291.0;
    CGFloat irBtnSize = 60.0;

    for (int i = 0; i < 5; i++) {
        IRButtonOverlay* btn = [[IRButtonOverlay alloc]
            initWithFrame:imageRectToView(irBtnXs[i], irBtnY, irBtnSize, irBtnSize)];
        btn.isSelected = (i == 0);
        int idx = i;
        btn.onSelect = ^{
            self.app->selectIR(idx);
            for (IRButtonOverlay* b in self->_irButtons) b.isSelected = NO;
            [self->_irButtons[idx] setIsSelected:YES];
            for (IRButtonOverlay* b in self->_irButtons) [b setNeedsDisplay:YES];
        };
        [content addSubview:btn];
        [_irButtons addObject:btn];
    }

    // --- REVERB CONTROL: 4 large knobs ---
    CGFloat bigKnob = 120.0;

    RotaryKnobOverlay* preDelayKnob = [[RotaryKnobOverlay alloc]
        initWithFrame:imageRectToView(578, 489, bigKnob, bigKnob)
                  min:0 max:500 value:0 label:@"PRE-DELAY"];
    preDelayKnob.onChange = ^(double v) { self.app->setParameter(ParameterID::PRE_DELAY, (float)v); };
    [content addSubview:preDelayKnob];

    RotaryKnobOverlay* timeKnob = [[RotaryKnobOverlay alloc]
        initWithFrame:imageRectToView(789, 489, bigKnob, bigKnob)
                  min:0.1 max:100 value:100 label:@"TIME"];
    timeKnob.onChange = ^(double v) { self.app->setParameter(ParameterID::REVERB_LENGTH, (float)v); };
    [content addSubview:timeKnob];

    RotaryKnobOverlay* sizeKnob = [[RotaryKnobOverlay alloc]
        initWithFrame:imageRectToView(579, 655, bigKnob, bigKnob)
                  min:0 max:1 value:0.5 label:@"SIZE"];
    sizeKnob.onChange = ^(double v) { self.app->setParameter(ParameterID::ROOM_SIZE, (float)v); };
    [content addSubview:sizeKnob];

    RotaryKnobOverlay* diffKnob = [[RotaryKnobOverlay alloc]
        initWithFrame:imageRectToView(789, 658, bigKnob, bigKnob)
                  min:0 max:1 value:0.5 label:@"DIFFUSION"];
    diffKnob.onChange = ^(double v) { self.app->setParameter(ParameterID::DIFFUSION, (float)v); };
    [content addSubview:diffKnob];

    // --- OUTPUT CONTROL: 3 faders ---
    CGFloat outFaderW = 36.0;
    CGFloat outFaderH = 320.0;
    CGFloat outFaderCenterY = 380.0;

    FaderOverlay* dryFader = [[FaderOverlay alloc]
        initWithFrame:imageRectToView(981, outFaderCenterY, outFaderW, outFaderH)
                  min:-80 max:0 value:-6 redHandle:NO];
    dryFader.onChange = ^(double v) { self.app->setParameter(ParameterID::DRY_LEVEL, (float)v); };
    [content addSubview:dryFader];

    FaderOverlay* wetFader = [[FaderOverlay alloc]
        initWithFrame:imageRectToView(1095, outFaderCenterY, outFaderW, outFaderH)
                  min:-80 max:0 value:-6 redHandle:NO];
    wetFader.onChange = ^(double v) { self.app->setParameter(ParameterID::WET_LEVEL, (float)v); };
    [content addSubview:wetFader];

    FaderOverlay* outputFader = [[FaderOverlay alloc]
        initWithFrame:imageRectToView(1205, outFaderCenterY, outFaderW, outFaderH)
                  min:-80 max:6 value:0 redHandle:YES];
    outputFader.onChange = ^(double v) { self.app->setParameter(ParameterID::OUTPUT_LEVEL, (float)v); };
    [content addSubview:outputFader];

    // --- VU METERS ---
    // Three VU meters at top center of the GUI image
    CGFloat vuW = 150.0, vuH = 100.0;
    CGFloat vuY = 115.0;  // vertical center of VU meter area

    _vuInput = [[VUMeterOverlay alloc] initWithFrame:
        imageRectToView(555, vuY, vuW, vuH)];
    [content addSubview:_vuInput];

    _vuProcess = [[VUMeterOverlay alloc] initWithFrame:
        imageRectToView(688, vuY, vuW, vuH)];
    [content addSubview:_vuProcess];

    _vuOutput = [[VUMeterOverlay alloc] initWithFrame:
        imageRectToView(822, vuY, vuW, vuH)];
    [content addSubview:_vuOutput];

    // --- TOGGLES ---
    CGFloat toggleW = 50.0, toggleH = 40.0;

    ToggleOverlay* reverseToggle = [[ToggleOverlay alloc]
        initWithFrame:imageRectToView(971, 582, toggleW, toggleH)];
    reverseToggle.onChange = ^(BOOL on) {
        self.app->setParameter(ParameterID::REVERSE, on ? 1.0f : 0.0f);
    };
    reverseToggle.wantsLayer = YES;
    [content addSubview:reverseToggle];

    ToggleOverlay* micToggle = [[ToggleOverlay alloc]
        initWithFrame:imageRectToView(1088, 582, toggleW, toggleH)];
    __weak ToggleOverlay* weakMicToggle = micToggle;
    micToggle.onChange = ^(BOOL on) {
        if (on) {
            self->_pendingMicToggle = weakMicToggle;
            ToggleOverlay* strongMic = weakMicToggle;
            // Defer to next run loop iteration so mouseDown completes first
            dispatch_async(dispatch_get_main_queue(), ^{
                NSMenu* deviceMenu = [[NSMenu alloc] initWithTitle:@"Select Input"];
                auto inputDevices = self.app->getAudioIO()->listInputDevices();
                if (inputDevices.empty()) {
                    NSMenuItem* noDevItem = [[NSMenuItem alloc] initWithTitle:@"No input devices found"
                        action:nil keyEquivalent:@""];
                    noDevItem.enabled = NO;
                    [deviceMenu addItem:noDevItem];
                } else {
                    for (size_t i = 0; i < inputDevices.size(); i++) {
                        NSString* name = [NSString stringWithUTF8String:inputDevices[i].name.c_str()];
                        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:name
                            action:@selector(selectInputDevice:) keyEquivalent:@""];
                        item.tag = (NSInteger)inputDevices[i].deviceId;
                        item.target = self;
                        [deviceMenu addItem:item];
                    }
                }
                [deviceMenu addItem:[NSMenuItem separatorItem]];
                NSMenuItem* cancelItem = [[NSMenuItem alloc] initWithTitle:@"Cancel"
                    action:@selector(cancelMicSelect:) keyEquivalent:@""];
                cancelItem.target = self;
                [deviceMenu addItem:cancelItem];

                // Show the menu anchored to the mic toggle view
                if (strongMic) {
                    NSPoint pt = NSMakePoint(0, strongMic.bounds.size.height);
                    [deviceMenu popUpMenuPositioningItem:nil atLocation:pt inView:strongMic];
                }
            });
        } else {
            self.app->enableMicInput(NO);
        }
    };
    micToggle.wantsLayer = YES;
    [content addSubview:micToggle];

    // --- TRANSPORT: Play, Stop, Bypass ---
    CGFloat tBtnSize = 44.0;

    TransportButtonOverlay* playBtn = [[TransportButtonOverlay alloc]
        initWithFrame:imageRectToView(1010, 678, tBtnSize, tBtnSize)];
    playBtn.wantsLayer = YES;
    playBtn.onPress = ^{
        if (self.app->isFilePlaying()) {
            self.app->playFile();
            return;
        }
        // Defer to next run loop so mouseDown completes first
        dispatch_async(dispatch_get_main_queue(), ^{
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            panel.title = @"Load Audio File";
            panel.message = @"Select an audio file to process through the Echo Chamber";
            panel.allowedContentTypes = @[
                [UTType typeWithFilenameExtension:@"wav"],
                [UTType typeWithFilenameExtension:@"mp3"],
                [UTType typeWithFilenameExtension:@"aiff"],
                [UTType typeWithFilenameExtension:@"aif"],
                [UTType typeWithFilenameExtension:@"m4a"],
                [UTType typeWithFilenameExtension:@"flac"]
            ];
            panel.allowsMultipleSelection = NO;
            panel.canChooseDirectories = NO;
            if ([panel runModal] == NSModalResponseOK) {
                NSString* path = panel.URL.path;
                self.app->loadAudioFile(path.UTF8String);
                self.app->playFile();
            }
        });
    };
    [content addSubview:playBtn];

    TransportButtonOverlay* stopBtn = [[TransportButtonOverlay alloc]
        initWithFrame:imageRectToView(1070, 678, tBtnSize, tBtnSize)];
    stopBtn.wantsLayer = YES;
    stopBtn.onPress = ^{ self.app->stopFile(); };
    [content addSubview:stopBtn];

    // Bypass toggle
    BypassButtonOverlay* bypassOverlay = [[BypassButtonOverlay alloc]
        initWithFrame:imageRectToView(1040, 730, 130, 30)];
    bypassOverlay.wantsLayer = YES;
    bypassOverlay.onChange = ^(BOOL on) {
        self.app->setBypass(on);
        self.app->setParameter(ParameterID::BYPASS, on ? 1.0f : 0.0f);
    };
    [content addSubview:bypassOverlay];

    // === Menu bar icon ===
    [self setupMenuBarIcon];

    // === Meter timer (30fps) ===
    _meterTimer = [NSTimer scheduledTimerWithTimeInterval:1.0/30.0
        target:self selector:@selector(updateMeters) userInfo:nil repeats:YES];

    [_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (NSString*)findGUIImage {
    NSArray* paths = @[
        @"resources/gui/mockv30.jpeg",
        [NSString stringWithFormat:@"%s/GOLD STAR ECHO CHAMBER vAG40/resources/gui/mockv30.jpeg",
            getenv("HOME") ?: ""],
        [NSString stringWithFormat:@"%s/Desktop/GoldStar Echo Chamber D30.13/resources/gui/mockv30.jpeg",
            getenv("HOME") ?: ""],
    ];

    // Also check inside .app bundle
    char exePath[4096];
    uint32_t exePathSize = sizeof(exePath);
    if (_NSGetExecutablePath(exePath, &exePathSize) == 0) {
        NSString* exe = [NSString stringWithUTF8String:exePath];
        NSString* bundlePath = [[exe stringByDeletingLastPathComponent]
            stringByAppendingPathComponent:@"../Resources/mockv30.jpeg"];
        paths = [paths arrayByAddingObject:bundlePath];
    }

    for (NSString* p in paths) {
        if ([[NSFileManager defaultManager] fileExistsAtPath:p]) {
            return p;
        }
    }
    NSLog(@"WARNING: GUI image not found");
    return nil;
}

- (void)setupMenuBarIcon {
    _statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];

    // Try to load the gold star icon
    NSString* homeDir = [NSString stringWithUTF8String:getenv("HOME") ?: ""];
    NSArray* iconPaths = @[
        [homeDir stringByAppendingPathComponent:@"GOLD STAR ECHO CHAMBER vAG40/resources/statusbar_icon.png"],
        @"resources/statusbar_icon.png",
    ];

    BOOL iconLoaded = NO;
    for (NSString* iconPath in iconPaths) {
        if ([[NSFileManager defaultManager] fileExistsAtPath:iconPath]) {
            NSImage* icon = [[NSImage alloc] initWithContentsOfFile:iconPath];
            // Load @2x for Retina
            NSString* icon2xPath = [[iconPath stringByDeletingPathExtension]
                stringByAppendingString:@"@2x.png"];
            if ([[NSFileManager defaultManager] fileExistsAtPath:icon2xPath]) {
                NSImageRep* rep2x = [[NSImageRep imageRepsWithContentsOfFile:icon2xPath] firstObject];
                if (rep2x) [icon addRepresentation:rep2x];
            }
            [icon setSize:NSMakeSize(18, 18)];
            [icon setTemplate:NO];
            _statusItem.button.image = icon;
            iconLoaded = YES;
            break;
        }
    }
    if (!iconLoaded) {
        _statusItem.button.title = @"★";
    }
    _statusItem.button.toolTip = @"Gold Star Echo Chamber";

    NSMenu* menu = [[NSMenu alloc] init];

    NSMenuItem* titleItem = [[NSMenuItem alloc] initWithTitle:@"Gold Star Echo Chamber vAG40"
                                                       action:nil keyEquivalent:@""];
    [titleItem setEnabled:NO];
    [menu addItem:titleItem];
    [menu addItem:[NSMenuItem separatorItem]];

    // Bypass toggle
    NSMenuItem* bypassItem = [[NSMenuItem alloc] initWithTitle:@"Bypass"
                                                        action:@selector(toggleBypassFromMenu:)
                                                 keyEquivalent:@"b"];
    [bypassItem setTarget:self];
    bypassItem.state = _app->isBypassed() ? NSControlStateValueOn : NSControlStateValueOff;
    bypassItem.tag = 100;
    [menu addItem:bypassItem];

    [menu addItem:[NSMenuItem separatorItem]];

    // IR selection submenu
    NSMenuItem* irMenuItem = [[NSMenuItem alloc] initWithTitle:@"Impulse Response"
                                                        action:nil keyEquivalent:@""];
    NSMenu* irSubmenu = [[NSMenu alloc] init];
    auto irs = _app->getAvailableIRs();
    for (int i = 0; i < (int)irs.size(); i++) {
        NSString* name = [[NSString stringWithUTF8String:irs[i].c_str()] lastPathComponent];
        name = [name stringByDeletingPathExtension];
        NSMenuItem* irItem = [[NSMenuItem alloc] initWithTitle:name
                                                         action:@selector(selectIRFromMenu:)
                                                  keyEquivalent:@""];
        [irItem setTarget:self];
        irItem.tag = i;
        if (i == _app->getCurrentIRIndex()) {
            irItem.state = NSControlStateValueOn;
        }
        [irSubmenu addItem:irItem];
    }
    [irMenuItem setSubmenu:irSubmenu];
    [menu addItem:irMenuItem];

    [menu addItem:[NSMenuItem separatorItem]];

    [menu addItemWithTitle:@"Show Window" action:@selector(showWindow:) keyEquivalent:@""];
    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];
    for (NSMenuItem* item in menu.itemArray) {
        if (!item.target && item.action) item.target = self;
    }
    _statusItem.menu = menu;
}

- (void)showWindow:(id)sender {
    [_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)updateMeters {
    if (!_app) return;
    _vuInput.level = _app->getInputLevel();
    _vuProcess.level = _app->getProcessLevel();
    _vuOutput.level = _app->getOutputLevel();
    [_vuInput setNeedsDisplay:YES];
    [_vuProcess setNeedsDisplay:YES];
    [_vuOutput setNeedsDisplay:YES];
}

- (void)selectInputDevice:(NSMenuItem*)sender {
    AudioDeviceID deviceId = (AudioDeviceID)sender.tag;
    _app->getAudioIO()->setInputDevice(deviceId);
    _app->enableMicInput(YES);
    NSLog(@"Selected input device: %@ (ID=%u)", sender.title, deviceId);
}

- (void)cancelMicSelect:(NSMenuItem*)sender {
    // User cancelled — turn toggle back off
    if (_pendingMicToggle) {
        _pendingMicToggle.isOn = NO;
        [_pendingMicToggle setNeedsDisplay:YES];
    }
}

- (void)toggleBypassFromMenu:(NSMenuItem*)sender {
    bool newState = !_app->isBypassed();
    _app->setBypass(newState);
    _app->setParameter(ParameterID::BYPASS, newState ? 1.0f : 0.0f);
    sender.state = newState ? NSControlStateValueOn : NSControlStateValueOff;
}

- (void)selectIRFromMenu:(NSMenuItem*)sender {
    int index = (int)sender.tag;
    _app->selectIR(index);

    // Update checkmarks
    NSMenu* submenu = sender.menu;
    for (NSMenuItem* item in submenu.itemArray) {
        item.state = (item.tag == index) ? NSControlStateValueOn : NSControlStateValueOff;
    }

    // Update IR button highlights in the main window
    for (int i = 0; i < (int)_irButtons.count; i++) {
        _irButtons[i].isSelected = (i == index);
        [_irButtons[i] setNeedsDisplay:YES];
    }
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return NO;
}

@end

// ============================================================================
// StandaloneUI implementation
// ============================================================================
StandaloneUI::StandaloneUI(StandaloneApp* app) : app_(app) {}
StandaloneUI::~StandaloneUI() {}

void StandaloneUI::runEventLoop() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        GoldStarAppDelegate* delegate = [[GoldStarAppDelegate alloc] init];
        delegate.app = app_;
        [NSApp setDelegate:delegate];

        [NSApp run];
    }
}
