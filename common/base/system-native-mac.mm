// Copyright 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "gfxstream/Optional.h"
#include "gfxstream/system/Memory.h"
#include "gfxstream/system/System.h"

#include <CoreFoundation/CoreFoundation.h>
//#include <IOKit/IOBSD.h>
#include <IOKit/IOKitLib.h>
//#include <AppKit/AppKit.h>

//#include <IOKit/kext/KextManager.h>
//#include <IOKit/storage/IOBlockStorageDevice.h>

#import <Foundation/Foundation.h>
#import <Foundation/NSProcessInfo.h>

#include <mach/mach_init.h>
#include <mach/thread_act.h>
#include <mach/mach_port.h>

#include <stdlib.h>
#include <sys/stat.h>

namespace gfxstream {
namespace base {

// From:
// https://stackoverflow.com/questions/49677034/thread-sleeps-too-long-on-os-x-when-in-background
void disableAppNap_macImpl(void) {
   if ([[NSProcessInfo processInfo]
        respondsToSelector:@selector(beginActivityWithOptions:reason:)]) {
      [[NSProcessInfo processInfo]
       beginActivityWithOptions:0x00FFFFFF
       reason:@"Not sleepy and don't want to nap"];
   }
}

// based on https://stackoverflow.com/questions/13893134/get-current-pthread-cpu-usage-mac-os-x
void cpuUsageCurrentThread_macImpl(
    uint64_t* user,
    uint64_t* sys) {

    kern_return_t kr;
    mach_msg_type_number_t count;
    mach_port_t thread;
    thread_basic_info_data_t info;

    thread = mach_thread_self();

    count = THREAD_BASIC_INFO_COUNT;
    kr = thread_info(
        thread,
        THREAD_BASIC_INFO,
        (thread_info_t)&info,
        &count);

    if (kr == KERN_SUCCESS) {
        *user =
            info.user_time.seconds * 1000000ULL +
            info.user_time.microseconds;
        *sys =
            info.system_time.seconds * 1000000ULL +
            info.system_time.microseconds;
    } else {
        fprintf(stderr, "%s: WARNING: could not get thread info for CPU usage\n", __func__);
    }

    mach_port_deallocate(mach_task_self(), thread);
}

}  // namespace base
}  // namespace gfxstream
