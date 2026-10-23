// ============================================================================
//  Alert.mm — UIAlertController 封装
// ============================================================================

#include "App/Alert.h"

#import <UIKit/UIKit.h>

#include <dispatch/dispatch.h>

#include "App/OverlayWindow.h"

namespace {

UIWindow *KeyWindow() {
    for (UIScene *scene in [UIApplication sharedApplication].connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]]) continue;
        UIWindowScene *ws = (UIWindowScene *)scene;
        // UIWindowScene.windows 是 iOS 15 才有的属性；13/14 走全局 windows。
        NSArray<UIWindow *> *windows = nil;
        if (@available(iOS 15.0, *)) {
            windows = ws.windows;
        } else {
            windows = [UIApplication sharedApplication].windows;
        }
        for (UIWindow *w in windows) {
            if (w.isKeyWindow) return w;
        }
        if (windows.count > 0) return windows.firstObject;
    }
    return [UIApplication sharedApplication].windows.firstObject;
}

UIViewController *TopController() {
    UIViewController *vc = KeyWindow().rootViewController;
    while (vc.presentedViewController) vc = vc.presentedViewController;
    return vc;
}

void PresentOnMain(UIViewController *alert) {
    dispatch_async(dispatch_get_main_queue(), ^{
        UIViewController *top = TopController();
        if (!top) return;
        [top presentViewController:alert animated:YES completion:nil];
    });
}

}  // namespace

namespace SMAlert {

void Show(const std::string &title, const std::string &message, const std::string &okTitle) {
    UIAlertController *alert = [UIAlertController
        alertControllerWithTitle:[NSString stringWithUTF8String:title.c_str()]
                         message:[NSString stringWithUTF8String:message.c_str()]
                  preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:[NSString stringWithUTF8String:okTitle.c_str()]
                                              style:UIAlertActionStyleDefault
                                            handler:nil]];
    PresentOnMain(alert);
}

void Confirm(const std::string &title, const std::string &message,
             const std::string &okTitle, const std::string &cancelTitle,
             std::function<void(bool)> callback) {
    UIAlertController *alert = [UIAlertController
        alertControllerWithTitle:[NSString stringWithUTF8String:title.c_str()]
                         message:[NSString stringWithUTF8String:message.c_str()]
                  preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:[NSString stringWithUTF8String:cancelTitle.c_str()]
                                              style:UIAlertActionStyleCancel
                                            handler:^(UIAlertAction *_) {
        if (callback) callback(false);
    }]];
    [alert addAction:[UIAlertAction actionWithTitle:[NSString stringWithUTF8String:okTitle.c_str()]
                                              style:UIAlertActionStyleDefault
                                            handler:^(UIAlertAction *_) {
        if (callback) callback(true);
    }]];
    PresentOnMain(alert);
}

void ShowInput(const std::string &title, const std::string &hint, const std::string &current,
               std::function<void(const std::string &)> callback) {
    UIAlertController *alert = [UIAlertController
        alertControllerWithTitle:[NSString stringWithUTF8String:title.c_str()]
                         message:[NSString stringWithUTF8String:hint.c_str()]
                  preferredStyle:UIAlertControllerStyleAlert];
    [alert addTextFieldWithConfigurationHandler:^(UITextField *field) {
        field.text = [NSString stringWithUTF8String:current.c_str()];
        field.placeholder = @"License / Card key";
        field.autocapitalizationType = UITextAutocapitalizationTypeNone;
        field.autocorrectionType = UITextAutocorrectionTypeNo;
        field.clearButtonMode = UITextFieldViewModeWhileEditing;
    }];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                              style:UIAlertActionStyleCancel
                                            handler:nil]];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK"
                                              style:UIAlertActionStyleDefault
                                            handler:^(UIAlertAction *_) {
        NSString *text = alert.textFields.firstObject.text ?: @"";
        const char *c = [text UTF8String];
        std::string value = c ? std::string(c) : std::string();
        // 回调在主线程（UIAlertAction 本来就在主线程）：调用方会直接写
        // license_input 这类被渲染线程读取的缓冲，跨线程写会撕裂字符串。
        if (callback) callback(value);
    }]];
    PresentOnMain(alert);
}

void Toast(const std::string &text) { SMOverlayShowToast(text); }

}  // namespace SMAlert
