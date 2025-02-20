#ifndef __WEBROGUE_STREAM_H
#define __WEBROGUE_STREAM_H

#include <stdlib.h>
#include <memory>

#include "render-utils/IOStream.h"

gfxstream::IOStream* makeWebrogueStream(int bufferSize);

#endif
