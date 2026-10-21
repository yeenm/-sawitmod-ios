// ============================================================================
//  OverlayWindow.mm — 悬浮菜单窗口 + 独立 GL 渲染循环 + 触摸转发
// ----------------------------------------------------------------------------
//  与 Android 版的对应关系：
//    Java Menu/SurfaceView  -> 本文件里的 UIWindow + SMGLView(CAEAGLLayer)
//    Java 触摸转发 + hook libinput -> 窗口 hitTest 穿透 + touchesBegan 等回调
//    hook eglSwapBuffers 画菜单 -> 自己的 EAGLContext + CADisplayLink 每帧画
//
//  这样 iOS 侧不需要 hook 游戏的渲染管线，也不会和 Unity 的 GL 状态打架：
//  菜单画在自己窗口的 framebuffer 里，再叠到游戏画面之上。
// ============================================================================

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES3/gl.h>
#import <OpenGLES/ES3/glext.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "App/OverlayWindow.h"
#include "App/Platform.h"
#include "Core/State.h"
#include "UI/MenuGlue.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_opengl3.h"

// ---------------------------------------------------------------------------
//  GL 资源
// ---------------------------------------------------------------------------
static EAGLContext *g_ctx = nil;
static GLuint g_fbo = 0;
static GLuint g_rbo = 0;
static GLint g_width = 0;
static GLint g_height = 0;
static BOOL g_imguiReady = NO;
static BOOL g_started = NO;

static UIWindow *g_window = nil;
static UIView *g_glView = nil;
static CADisplayLink *g_link = nil;
static id g_lifecycleObservers[2] = {nil, nil};
static id g_orientationObserver = nil;
static BOOL g_orientationStarted = NO;

// ---------------------------------------------------------------------------
//  触摸队列：UIKit 回调在主线程，渲染也在主线程；仍然先入队再在帧内统一喂
//  ImGui，避免在 NewFrame 之外改 io 状态。
// ---------------------------------------------------------------------------
namespace {
struct TouchEvent {
    bool down;
    float x;
    float y;
};
std::mutex g_touchMutex;
std::vector<TouchEvent> g_touchQueue;

void PushTouch(bool down, float x, float y) {
    std::lock_guard<std::mutex> lock(g_touchMutex);
    g_touchQueue.push_back({down, x, y});
}

void FlushTouches() {
    std::vector<TouchEvent> events;
    {
        std::lock_guard<std::mutex> lock(g_touchMutex);
        events.swap(g_touchQueue);
    }
    ImGuiIO &io = ImGui::GetIO();
    for (const TouchEvent &e : events) {
        io.AddMousePosEvent(e.x, e.y);
        io.AddMouseButtonEvent(0, e.down);
    }
}

NSString *ToNSString(const std::string &s) {
    return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding] ?: @"";
}
}  // namespace

static UIWindowScene *SMFindWindowScene(void) {
    for (UIScene *scene in [UIApplication sharedApplication].connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]]) continue;
        UIWindowScene *ws = (UIWindowScene *)scene;
        if (ws.activationState == UISceneActivationStateForegroundActive) return ws;
    }
    for (UIScene *scene in [UIApplication sharedApplication].connectedScenes) {
        if ([scene isKindOfClass:[UIWindowScene class]]) return (UIWindowScene *)scene;
    }
    return nil;
}

// ---------------------------------------------------------------------------
//  渲染
// ---------------------------------------------------------------------------
static void SMCreateFramebuffer(UIView *view) {
    if (!g_ctx || !view || !view.layer) return;
    [EAGLContext setCurrentContext:g_ctx];

    if (g_fbo) {
        glDeleteFramebuffers(1, &g_fbo);
        g_fbo = 0;
    }
    if (g_rbo) {
        glDeleteRenderbuffers(1, &g_rbo);
        g_rbo = 0;
    }

    glGenFramebuffers(1, &g_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glGenRenderbuffers(1, &g_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, g_rbo);
    [g_ctx renderbufferStorage:GL_RENDERBUFFER fromDrawable:(CAEAGLLayer *)view.layer];
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g_rbo);

    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &g_width);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &g_height);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        NSLog(@"[SawitMod] framebuffer incomplete: 0x%x", status);
    }

    g_RealScreenWidth.store((float)g_width);
    g_RealScreenHeight.store((float)g_height);
    NSLog(@"[SawitMod] GL drawable %dx%d", g_width, g_height);
}

static void SMRenderFrame(void) {
    if (!g_ctx || !g_glView || g_width <= 0 || g_height <= 0) return;
    if (!g_AppInForeground.load()) return;

    if (![EAGLContext setCurrentContext:g_ctx]) return;

    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, g_width, g_height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!g_imguiReady) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;  // 不写 imgui.ini，设置走 NSUserDefaults
        if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
            NSLog(@"[SawitMod] ImGui_ImplOpenGL3_Init failed");
            return;
        }
        SMMenu::Init((float)g_width, (float)g_height, (float)[UIScreen mainScreen].scale);
        g_imguiReady = YES;
    }

    // 设置页改缩放：与 Android 版一样，只在这一帧消费一次，避免每帧重建字体。
    const float requestedScale = g_RequestedUiScale.exchange(-1.0f);
    if (requestedScale > 0.0f) {
        SMMenu::ApplyScale(requestedScale);
    }

    ImGui_ImplOpenGL3_NewFrame();

    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)g_width, (float)g_height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = (g_link && g_link.duration > 0.0) ? (float)g_link.duration : (1.0f / 60.0f);

    FlushTouches();

    ImGui::NewFrame();
    SMMenu::Draw();
    ImGui::Render();

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    [g_ctx presentRenderbuffer:GL_RENDERBUFFER];
}

// ---------------------------------------------------------------------------
//  UIView：CAEAGLLayer 承载 + 菜单区域命中
// ---------------------------------------------------------------------------
@interface SMGLView : UIView
@end

@implementation SMGLView

+ (Class)layerClass {
    return [CAEAGLLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.opaque = NO;
        self.backgroundColor = [UIColor clearColor];
        self.multipleTouchEnabled = YES;
        self.userInteractionEnabled = YES;
        CAEAGLLayer *layer = (CAEAGLLayer *)self.layer;
        layer.opaque = NO;
        layer.drawableProperties = @{
            kEAGLDrawablePropertyRetainedBacking : @NO,
            kEAGLDrawablePropertyColorFormat : kEAGLColorFormatRGBA8,
        };
        layer.contentsScale = [UIScreen mainScreen].scale;
    }
    return self;
}

- (void)layoutSubviews {
    [super layoutSubviews];
    SMCreateFramebuffer(self);
}

- (BOOL)pointInside:(CGPoint)point withEvent:(UIEvent *)event {
    (void)event;
    if (!g_imguiReady) return NO;
    const CGFloat scale = (self.bounds.size.width > 1.0)
                              ? (CGFloat)g_width / self.bounds.size.width
                              : [UIScreen mainScreen].scale;
    return SMOverlay::PointInsideMenu((float)(point.x * scale), (float)(point.y * scale));
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *touch in touches) {
        const CGPoint p = [touch locationInView:self];
        const CGFloat scale = (self.bounds.size.width > 1.0)
                                  ? (CGFloat)g_width / self.bounds.size.width
                                  : [UIScreen mainScreen].scale;
        PushTouch(true, (float)(p.x * scale), (float)(p.y * scale));
    }
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *touch in touches) {
        const CGPoint p = [touch locationInView:self];
        const CGFloat scale = (self.bounds.size.width > 1.0)
                                  ? (CGFloat)g_width / self.bounds.size.width
                                  : [UIScreen mainScreen].scale;
        PushTouch(false, (float)(p.x * scale), (float)(p.y * scale));
        PushTouch(true, (float)(p.x * scale), (float)(p.y * scale));
    }
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *touch in touches) {
        const CGPoint p = [touch locationInView:self];
        const CGFloat scale = (self.bounds.size.width > 1.0)
                                  ? (CGFloat)g_width / self.bounds.size.width
                                  : [UIScreen mainScreen].scale;
        PushTouch(false, (float)(p.x * scale), (float)(p.y * scale));
    }
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self touchesEnded:touches withEvent:event];
}

@end

@interface SMOverlayVC : UIViewController
@end

@implementation SMOverlayVC
- (void)loadView {
    SMGLView *view = [[SMGLView alloc] initWithFrame:[UIScreen mainScreen].bounds];
    view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.view = view;
    g_glView = view;
}
- (BOOL)prefersStatusBarHidden { return YES; }
- (UIStatusBarStyle)preferredStatusBarStyle { return UIStatusBarStyleLightContent; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
@end

// ---------------------------------------------------------------------------
//  UIWindow：菜单以外的触摸直接穿透到游戏窗口
// ---------------------------------------------------------------------------
@interface SMOverlayWindow : UIWindow
@end

@implementation SMOverlayWindow
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {
    UIView *hit = [super hitTest:point withEvent:event];
    // 注意：本窗口的 rootViewController.view 就是 SMGLView 本身。
    // 菜单内的触摸会让 super 返回 GLView —— 那正是我们要的结果，不能再把它当成
    // "没命中"丢掉（之前这里把 rootViewController.view 一起 return nil，导致菜单
    // 看得见但点不动）。菜单外时 GLView 的 pointInside 返回 NO，super 会返回
    // window 自己，下面这条就能把触摸放行给游戏窗口。
    if (hit == self) {
        return nil;
    }
    return hit;
}
@end

// ---------------------------------------------------------------------------
//  CADisplayLink 目标
// ---------------------------------------------------------------------------
@interface SMDisplayLinkTarget : NSObject
- (void)tick:(CADisplayLink *)link;
@end

@implementation SMDisplayLinkTarget
- (void)tick:(CADisplayLink *)link {
    (void)link;
    @autoreleasepool {
        SMRenderFrame();
    }
}
@end

static SMDisplayLinkTarget *g_linkTarget = nil;

// ---------------------------------------------------------------------------
//  轻提示
// ---------------------------------------------------------------------------
void SMOverlayShowToast(const std::string &text) {
    NSString *msg = ToNSString(text);
    dispatch_async(dispatch_get_main_queue(), ^{
        UIView *host = g_glView ?: g_window;
        if (!host) return;

        UILabel *label = [[UILabel alloc] initWithFrame:CGRectZero];
        label.text = msg;
        label.textColor = [UIColor colorWithWhite:1.0 alpha:0.96];
        label.backgroundColor = [UIColor colorWithWhite:0.12 alpha:0.86];
        label.font = [UIFont systemFontOfSize:15.0 weight:UIFontWeightMedium];
        label.textAlignment = NSTextAlignmentCenter;
        label.numberOfLines = 0;
        label.layer.cornerRadius = 10.0;
        label.layer.masksToBounds = YES;
        label.userInteractionEnabled = NO;
        // GLView 可能为了适配横屏游戏做了 90° 旋转；提示文字要反向抵消，否则是躺着的。
        if (g_glView) label.transform = CGAffineTransformInvert(g_glView.transform);

        const CGFloat maxWidth = host.bounds.size.width - 48.0;
        CGSize size = [msg boundingRectWithSize:CGSizeMake(maxWidth, CGFLOAT_MAX)
                                        options:NSStringDrawingUsesLineFragmentOrigin
                                     attributes:@{NSFontAttributeName : label.font}
                                        context:nil].size;
        const CGFloat width = MIN(maxWidth, size.width + 32.0);
        const CGFloat height = MAX(36.0, size.height + 18.0);
        label.frame = CGRectMake((host.bounds.size.width - width) * 0.5, 64.0, width, height);
        label.alpha = 0.0;
        [host addSubview:label];

        [UIView animateWithDuration:0.18
                         animations:^{ label.alpha = 1.0; }
                         completion:^(BOOL finished) {
                             [UIView animateWithDuration:0.35
                                              delay:1.2
                                            options:UIViewAnimationOptionCurveEaseIn
                                         animations:^{ label.alpha = 0.0; }
                                         completion:^(BOOL done) { [label removeFromSuperview]; }];
                         }];
    });
}

// ---------------------------------------------------------------------------
//  屏幕方向
// ---------------------------------------------------------------------------
//  Unity 横屏游戏常常把 App 的 UIKit 方向留在竖屏，只旋转自己的 GL 视图。
//  如果我们的窗口按竖屏铺满，菜单就会相对游戏"躺着"。按设备方向判断：
//    * 设备横屏、窗口竖屏  -> 把 GLView 旋转 90°，bounds 换成横屏
//    * 设备竖屏、窗口横屏  -> 反向旋转
//    * 两者一致            -> 不旋转
//  旋转后 bounds 变化会触发 layoutSubviews -> 重建横屏 framebuffer，
//  ImGui 的 DisplaySize 与触摸坐标也随之变成横屏。
static void SMUpdateOrientation(void) {
    if (!g_window || !g_glView) return;

    const CGSize screen = g_window.bounds.size;
    const BOOL windowLandscape = screen.width > screen.height;

    const UIDeviceOrientation dev = [UIDevice currentDevice].orientation;
    BOOL deviceLandscape = NO;
    switch (dev) {
        case UIDeviceOrientationLandscapeLeft:
        case UIDeviceOrientationLandscapeRight:
            deviceLandscape = YES;
            break;
        case UIDeviceOrientationPortrait:
        case UIDeviceOrientationPortraitUpsideDown:
            deviceLandscape = NO;
            break;
        default:
            // 平放/未知：保持现状，等下一次方向通知
            return;
    }

    if (deviceLandscape == windowLandscape) {
        g_glView.transform = CGAffineTransformIdentity;
        g_glView.bounds = CGRectMake(0, 0, screen.width, screen.height);
    } else {
        const CGFloat angle =
            (dev == UIDeviceOrientationLandscapeLeft) ? (CGFloat)M_PI_2 : (CGFloat)-M_PI_2;
        g_glView.transform = CGAffineTransformMakeRotation(angle);
        g_glView.bounds = CGRectMake(0, 0, screen.height, screen.width);
    }
    g_glView.center = CGPointMake(screen.width * 0.5, screen.height * 0.5);
    [g_glView setNeedsLayout];
    NSLog(@"[SawitMod] orientation: dev=%ld windowLandscape=%d -> bounds %.0fx%.0f",
          (long)dev, windowLandscape, g_glView.bounds.size.width, g_glView.bounds.size.height);
}

// ---------------------------------------------------------------------------
//  对外接口
// ---------------------------------------------------------------------------
namespace SMOverlay {

bool Start() {
    if (g_started) return true;
    if (![NSThread isMainThread]) {
        __block bool result = false;
        dispatch_sync(dispatch_get_main_queue(), ^{ result = Start(); });
        return result;
    }

    // iOS 13+ 的窗口必须绑定 windowScene，否则不会显示。App 冷启动时 scene 可能
    // 还没连上，返回 false 让调用方稍后重试（Bootstrap 里带重试）。
    UIWindowScene *scene = SMFindWindowScene();
    if (!scene) {
        NSLog(@"[SawitMod] windowScene 尚未就绪，稍后重试 overlay");
        return false;
    }

    g_ctx = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
    if (!g_ctx) {
        NSLog(@"[SawitMod] EAGLContext(ES3) 创建失败，菜单无法绘制");
        return false;
    }
    [EAGLContext setCurrentContext:g_ctx];

    SMOverlayWindow *window = [[SMOverlayWindow alloc] initWithWindowScene:scene];
    window.frame = scene.coordinateSpace.bounds;
    window.windowLevel = UIWindowLevelAlert + 100.0;
    window.backgroundColor = [UIColor clearColor];
    window.rootViewController = [[SMOverlayVC alloc] init];
    window.hidden = NO;
    g_window = window;

    // 让 GLView 拿到 bounds 之后建 framebuffer
    [window.rootViewController.view layoutIfNeeded];

    // 横屏游戏常见做法：App 的 UIKit 方向仍是竖屏，Unity 只旋转自己的 GL 视图。
    // 我们的窗口如果按竖屏铺满，菜单就会"躺着"。这里按设备方向把 GL 视图整体
    // 旋转 90° 并把 bounds 换成横屏尺寸，渲染尺寸和触摸坐标会自动跟着变。
    [[UIDevice currentDevice] beginGeneratingDeviceOrientationNotifications];
    g_orientationStarted = YES;
    SMUpdateOrientation();
    g_orientationObserver = [[NSNotificationCenter defaultCenter]
        addObserverForName:UIDeviceOrientationDidChangeNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification *note) {
        (void)note;
        SMUpdateOrientation();
    }];

    if (g_glView) SMCreateFramebuffer(g_glView);

    g_linkTarget = [[SMDisplayLinkTarget alloc] init];
    g_link = [CADisplayLink displayLinkWithTarget:g_linkTarget selector:@selector(tick:)];
    if (@available(iOS 15.0, *)) {
        g_link.preferredFrameRateRange = CAFrameRateRangeMake(30.0, 60.0, 60.0);
    } else {
        g_link.preferredFramesPerSecond = 60;
    }
    [g_link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];

    // 前后台：后台暂停渲染，回来继续。
    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    id bg = [center addObserverForName:UIApplicationDidEnterBackgroundNotification
                                object:nil
                                 queue:[NSOperationQueue mainQueue]
                            usingBlock:^(NSNotification *note) {
        (void)note;
        SetForeground(false);
    }];
    id fg = [center addObserverForName:UIApplicationWillEnterForegroundNotification
                                object:nil
                                 queue:[NSOperationQueue mainQueue]
                            usingBlock:^(NSNotification *note) {
        (void)note;
        SetForeground(true);
    }];
    g_lifecycleObservers[0] = bg;
    g_lifecycleObservers[1] = fg;

    g_started = YES;
    NSLog(@"[SawitMod] overlay started (%dx%d @ %.0fx)", g_width, g_height,
          [UIScreen mainScreen].scale);
    return true;
}

void Stop() {
    if (!g_started) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        [g_link invalidate];
        g_link = nil;
        for (int i = 0; i < 2; ++i) {
            if (g_lifecycleObservers[i]) {
                [[NSNotificationCenter defaultCenter] removeObserver:g_lifecycleObservers[i]];
                g_lifecycleObservers[i] = nil;
            }
        }
        if (g_orientationObserver) {
            [[NSNotificationCenter defaultCenter] removeObserver:g_orientationObserver];
            g_orientationObserver = nil;
        }
        if (g_orientationStarted) {
            [[UIDevice currentDevice] endGeneratingDeviceOrientationNotifications];
            g_orientationStarted = NO;
        }
        if (g_imguiReady) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui::DestroyContext();
            g_imguiReady = NO;
        }
        g_window.hidden = YES;
        g_window = nil;
        g_glView = nil;
        g_started = NO;
    });
}

bool IsRunning() { return g_started; }

float WidthPx() { return (float)g_width; }
float HeightPx() { return (float)g_height; }
float Scale() {
    UIView *view = g_glView;
    if (view && view.bounds.size.width > 1.0) return (float)g_width / (float)view.bounds.size.width;
    return (float)[UIScreen mainScreen].scale;
}

bool PointInsideMenu(float px, float py) {
    MenuBounds menu{};
    MenuBounds mini{};
    {
        std::lock_guard<std::mutex> lock(g_MenuBoundsMutex);
        menu = g_MenuRect;
        mini = g_MinRect;
    }
    auto contains = [](const MenuBounds &r, float x, float y) {
        return r.active && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
    };
    return contains(menu, px, py) || contains(mini, px, py);
}

void ShowToast(const std::string &text) { SMOverlayShowToast(text); }

void SetForeground(bool foreground) {
    g_AppInForeground.store(foreground);
    g_link.paused = !foreground;
}

}  // namespace SMOverlay
