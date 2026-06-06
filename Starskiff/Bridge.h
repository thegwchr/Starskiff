//
//  Bridge.h
//  Starskiff
//
//  Created by Bat.bat on 8/7/2024.
//  Copyright © 2024 Feixiao. All rights reserved.
//

#pragma once

// MARK: NSMenuItem Private API
#import <AppKit/AppKit.h>

@interface NSMenuItem ()
- (BOOL)_canBeHighlighted;
@end


// MARK: rtw88 API
#include "../ClientKit/Api.h"
