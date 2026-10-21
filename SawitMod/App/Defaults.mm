// ============================================================================
//  Defaults.mm — NSUserDefaults 封装
// ============================================================================

#include "App/Defaults.h"

#import <Foundation/Foundation.h>

namespace {

NSUserDefaults *Store() {
    static NSUserDefaults *defaults = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        defaults = [NSUserDefaults standardUserDefaults];
    });
    return defaults;
}

NSString *Key(const std::string &key) {
    return [NSString stringWithUTF8String:key.c_str()] ?: @"";
}

}  // namespace

namespace SMDefaults {

bool GetBool(const std::string &key, bool def) {
    id value = [Store() objectForKey:Key(key)];
    return value ? [Store() boolForKey:Key(key)] : def;
}

void SetBool(const std::string &key, bool value) { [Store() setBool:value forKey:Key(key)]; }

int GetInt(const std::string &key, int def) {
    id value = [Store() objectForKey:Key(key)];
    return value ? (int)[Store() integerForKey:Key(key)] : def;
}

void SetInt(const std::string &key, int value) { [Store() setInteger:value forKey:Key(key)]; }

std::string GetString(const std::string &key, const std::string &def) {
    NSString *value = [Store() stringForKey:Key(key)];
    if (!value) return def;
    const char *c = [value UTF8String];
    return c ? std::string(c) : def;
}

void SetString(const std::string &key, const std::string &value) {
    [Store() setObject:[NSString stringWithUTF8String:value.c_str()] forKey:Key(key)];
}

void Remove(const std::string &key) { [Store() removeObjectForKey:Key(key)]; }

void Synchronize() { [Store() synchronize]; }

}  // namespace SMDefaults
