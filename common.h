#ifndef _COMMON_H_
#define _COMMON_H_

#include <iostream>
#include <fstream>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

class Sender {
public:
    virtual ~Sender() {}
    virtual size_t Send(void* data, size_t sz) = 0;
};

class PacketHandler {
public:
    virtual ~PacketHandler() {}
    virtual void InitSend(Sender& s) = 0;
    virtual int32_t HandlePacket(Sender& s, short type, void* data, int32_t length) = 0;
};

struct pkt_base {
    short length;
    short type;
};

struct VTP_BEGIN_FILE {
    pkt_base hdr = {8, 0x10};
    uint32_t size;
    uint32_t flag;
};

struct VTP_FILE_CONTENT {
    pkt_base hdr = {4, 0x11};
    uint8_t buf[1024];
};

struct VTP_FILE_END {
    pkt_base hdr = {4, 0x12};
};

struct VTP_DOWN_FILE {
    pkt_base hdr = {4, 0x13};
};

struct VTP_DOWN_CONTINE {
    pkt_base hdr = {4, 0x14};
};

struct VTP_INSTALL_VPK {
    pkt_base hdr = {16, 0x20};
    uint32_t total_size_l = 0;
    uint32_t total_size_h = 0;
    uint32_t flag = 0;
};

struct VTP_VPK_CONTENT {
    pkt_base hdr = {4, 0x21};
};

struct VTP_INSTALL_VPK_END {
    pkt_base hdr = {4, 0x22};
};

struct VTRP_RES {
    short length = 8;
    short type = 0;
    int result = 0;
};

inline void send_resp(Sender& s, short type, int result) {
    VTRP_RES res;
    res.type = type;
    res.result = result;
    s.Send(&res, 8);
}

#endif
