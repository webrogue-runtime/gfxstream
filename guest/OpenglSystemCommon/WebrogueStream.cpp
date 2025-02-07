#include "WebrogueStream.h"
#include "../../include/GFXSTREAM_webrogue_unimplemented.h"

static const size_t kReadSize = 512 * 1024;
static const size_t kWriteOffset = kReadSize;

class WebrogueStream : public gfxstream::IOStream {
public:
    explicit WebrogueStream(size_t bufsize );
    
    ~WebrogueStream();

    virtual void* allocBuffer(size_t minSize) {
        // Add dedicated read buffer space at the front of the buffer.
        minSize += kReadSize;

        size_t allocSize = (m_bufsize < minSize ? minSize : m_bufsize);
        if (!m_buf) {
            m_buf = (unsigned char *)malloc(allocSize);
        }
        else if (m_bufsize < allocSize) {
            unsigned char *p = (unsigned char *)realloc(m_buf, allocSize);
            if (p != NULL) {
                m_buf = p;
                m_bufsize = allocSize;
            } else {
                printf("realloc (%zu) failed\n", allocSize);
                free(m_buf);
                m_buf = NULL;
                m_bufsize = 0;
            }
        }

        return m_buf + kWriteOffset;
    }
    virtual int commitBuffer(size_t size) { GFXSTREAM_NOT_IMPLEMENTED; }
    virtual const unsigned char* readFully(void* buf, size_t len) { GFXSTREAM_NOT_IMPLEMENTED; }

    virtual int writeFully(const void* buf, size_t len) { GFXSTREAM_NOT_IMPLEMENTED; }

    virtual void* getDmaForReading(uint64_t guest_paddr) { return nullptr; }
    virtual void unlockDma(uint64_t guest_paddr) {}

    virtual void onSave(gfxstream::guest::Stream* stream) { GFXSTREAM_NOT_IMPLEMENTED; }
    virtual unsigned char* onLoad(gfxstream::guest::Stream* stream) { GFXSTREAM_NOT_IMPLEMENTED; }

    virtual const unsigned char *readRaw(void *buf, size_t *inout_len) { GFXSTREAM_NOT_IMPLEMENTED; }
};

WebrogueStream::WebrogueStream(size_t bufSize) :
    gfxstream::IOStream(bufSize)
{
}

gfxstream::IOStream* makeWebrogueStream(int bufferSize) {
    return new WebrogueStream(bufferSize);
}