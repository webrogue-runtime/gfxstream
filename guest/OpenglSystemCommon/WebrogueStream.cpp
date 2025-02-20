#include "WebrogueStream.h"
#include <cstdint>
#include "../../include/GFXSTREAM_webrogue_unimplemented.h"

__attribute__((import_name("commit-buffer")))
__attribute__((import_module("webrogue-gfx"))) void
imported_webrogue_gfx_commit_buffer(const void* buf, uint32_t len);


__attribute__((import_name("ret-buffer-read")))
__attribute__((import_module("webrogue-gfx"))) void
imported_webrogue_gfx_ret_buffer_read(const void* buf, uint32_t len);

class WebrogueStream : public gfxstream::IOStream {
public:
    explicit WebrogueStream(size_t bufsize):
        gfxstream::IOStream(bufsize) {}
    
    ~WebrogueStream() {};

    virtual void* allocBuffer(size_t minSize) {
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
                free(m_buf);
                m_buf = NULL;
                m_bufsize = 0;
            }
        }

        return m_buf;
    }
    virtual int commitBuffer(size_t size) { 
        if (size == 0) return 0;
        return writeFully(m_buf, size);
    }
    virtual const unsigned char* readFully(void* buf, size_t len) { 
        imported_webrogue_gfx_ret_buffer_read(buf, len);
        return (const unsigned char*)buf;
    }
    virtual int writeFully(const void* buf, size_t len) {
        imported_webrogue_gfx_commit_buffer(buf, len);
        return len;
    }

    virtual void* getDmaForReading(uint64_t guest_paddr) { return nullptr; }
    virtual void unlockDma(uint64_t guest_paddr) {}

    virtual void onSave(gfxstream::guest::Stream* stream) { GFXSTREAM_NOT_IMPLEMENTED; }
    virtual unsigned char* onLoad(gfxstream::guest::Stream* stream) { GFXSTREAM_NOT_IMPLEMENTED; }

    virtual const unsigned char *readRaw(void *buf, size_t *inout_len) { GFXSTREAM_NOT_IMPLEMENTED; }
};

gfxstream::IOStream* makeWebrogueStream(int bufferSize) {
    return new WebrogueStream(bufferSize);
}
