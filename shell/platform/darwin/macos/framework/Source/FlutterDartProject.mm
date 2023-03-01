// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "flutter/shell/platform/darwin/macos/framework/Headers/FlutterDartProject.h"
#import "flutter/shell/platform/darwin/macos/framework/Source/FlutterDartProject_Internal.h"

#include "updater.h"

#include <vector>

#include "flutter/shell/platform/common/engine_switches.h"

static NSString* const kICUBundlePath = @"icudtl.dat";
static NSString* const kAppBundleIdentifier = @"io.flutter.flutter.app";

@implementation FlutterDartProject {
  NSBundle* _dartBundle;
  NSString* _assetsPath;
  NSString* _ICUDataPath;
}

- (instancetype)init {
  return [self initWithPrecompiledDartBundle:nil];
}

- (instancetype)initWithPrecompiledDartBundle:(NSBundle*)bundle {
  self = [super init];
  NSAssert(self, @"Super init cannot be nil");

  _dartBundle = bundle ?: [NSBundle bundleWithIdentifier:kAppBundleIdentifier];
  if (_dartBundle == nil) {
    // The bundle isn't loaded and can't be found by bundle ID. Find it by path.
    _dartBundle = [NSBundle bundleWithURL:[NSBundle.mainBundle.privateFrameworksURL
                                              URLByAppendingPathComponent:@"App.framework"]];
  }
  NSLog(@"Hack in HERE");

  auto fm = [NSFileManager defaultManager];

  // This should come from the application name.
  auto path = @"~/Library/Application Support/flutter_updater";
  auto expandedPath = [path stringByExpandingTildeInPath];
  auto cacheDir = [expandedPath stringByAppendingPathComponent:@"cache"];
  NSLog(@"Cache dir: %@", cacheDir);
  NSError* error = nil;
  BOOL success = [fm createDirectoryAtPath:cacheDir
               withIntermediateDirectories:YES
                                attributes:nil
                                     error:&error];
  if (!success) {
    NSLog(@"Error: %@", error);
  }

  auto bootDir = [expandedPath stringByAppendingPathComponent:@"boot"];
  success = [fm createDirectoryAtPath:bootDir
          withIntermediateDirectories:YES
                           attributes:nil
                                error:&error];
  if (!success) {
    NSLog(@"Error: %@", error);
  }

  auto bootDirUrl = [NSURL fileURLWithPath:bootDir];
  auto appFrameworkUrl = [bootDirUrl URLByAppendingPathComponent:@"App.framework"];

  [fm removeItemAtPath:appFrameworkUrl.path error:&error];
  if (!success) {
    NSLog(@"Error: %@", error);
  }

  success = [fm copyItemAtURL:_dartBundle.bundleURL toURL:appFrameworkUrl error:&error];
  if (!success) {
    NSLog(@"Error: %@", error);
  }

  auto dylibUrl = [appFrameworkUrl URLByAppendingPathComponent:@"Versions/Current/App"];
  [fm removeItemAtPath:dylibUrl.path error:&error];
  if (!success) {
    NSLog(@"Error: %@", error);
  }

  NSString* clientId = @"client_id";
  // char *active_version(const char *client_id, const char *cache_dir);
  update(clientId.UTF8String, cacheDir.UTF8String);

  // char *active_path(const char *client_id, const char *cache_dir);
  char* cActivePath = active_path(clientId.UTF8String, cacheDir.UTF8String);
  if (cActivePath != NULL) {
    NSString* activePath = [NSString stringWithUTF8String:cActivePath];
    free_string(cActivePath);
    NSLog(@"SUCCESS, got active_path: %@", activePath);
    auto activeUrl = [NSURL fileURLWithPath:activePath];
    success = [fm copyItemAtURL:activeUrl toURL:dylibUrl error:&error];
    if (!success) {
      NSLog(@"Error: %@", error);
    } else {
      NSLog(@"SUCCESS, booting from active_path: %@", activePath);
      char* cVersion = active_version(clientId.UTF8String, cacheDir.UTF8String);
      if (cVersion != NULL) {
        NSString* version = [NSString stringWithUTF8String:cVersion];
        free_string(cVersion);
        NSLog(@"Running version: %@", version);
      }

      _dartBundle = [NSBundle bundleWithURL:appFrameworkUrl];
    }

  } else {
    NSLog(@"FAIL, got active_path: %@", @"NULL");
  }

  if (!_dartBundle.isLoaded) {
    [_dartBundle load];
  }
  _dartEntrypointArguments = [[NSProcessInfo processInfo] arguments];
  // Remove the first element as it's the binary name
  _dartEntrypointArguments = [_dartEntrypointArguments
      subarrayWithRange:NSMakeRange(1, _dartEntrypointArguments.count - 1)];
  return self;
}

- (instancetype)initWithAssetsPath:(NSString*)assets ICUDataPath:(NSString*)icuPath {
  self = [super init];
  NSAssert(self, @"Super init cannot be nil");
  _assetsPath = assets;
  _ICUDataPath = icuPath;
  return self;
}

- (NSString*)assetsPath {
  if (_assetsPath) {
    return _assetsPath;
  }

  // If there's no App.framework, fall back to checking the main bundle for assets.
  NSBundle* assetBundle = _dartBundle ?: [NSBundle mainBundle];
  NSString* flutterAssetsName = [assetBundle objectForInfoDictionaryKey:@"FLTAssetsPath"];
  if (flutterAssetsName == nil) {
    flutterAssetsName = @"flutter_assets";
  }
  NSString* path = [assetBundle pathForResource:flutterAssetsName ofType:@""];
  if (!path) {
    NSLog(@"Failed to find path for \"%@\"", flutterAssetsName);
  }
  return path;
}

- (NSString*)ICUDataPath {
  if (_ICUDataPath) {
    return _ICUDataPath;
  }

  NSString* path = [[NSBundle bundleForClass:[self class]] pathForResource:kICUBundlePath
                                                                    ofType:nil];
  if (!path) {
    NSLog(@"Failed to find path for \"%@\"", kICUBundlePath);
  }
  return path;
}

@end
