#include "common.h"
#include "copy_handler.h"
#include "install_handler.h"

class LocalSender : public Sender {
public:
    LocalSender(int32_t client) {
        remote = client;
    }
    
    size_t Send(void* data, size_t length) {
        return send(remote, data, length, 0);
    }
    
protected:
    int32_t remote;
};

void show_usage(char* cmd) {
    std::cout << cmd << " [ip] copy [local_file] [remote_file]" << std::endl;
    std::cout << cmd << " [ip] install [local_vpk]" << std::endl;
}

int32_t main(int32_t argc, char* argv[]) {
    if(argc < 3) {
        show_usage(argv[0]);
        return 0;
    }
    PacketHandler* ph = nullptr;
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1340);
    addr.sin_addr.s_addr = inet_addr(argv[1]);
    if(addr.sin_addr.s_addr == 0xffffffff) {
        show_usage(argv[0]);
        return 0;
    }
    if(strcmp(argv[2], "copy") == 0) {
        if(argc < 5) {
            show_usage(argv[0]);
            return 0;
        }
        auto ch = new CopyHandler();
        if(!ch->Load(argv[3], argv[4])) {
            std::cout << "local file " << argv[3] << " load fail." << std::endl;
            delete ch;
            return 0;
        }
        ph = ch;
    } else if(strcmp(argv[2], "install") == 0) {
        if(argc < 4) {
            show_usage(argv[0]);
            return 0;
        }
        auto ih = new InstallHandler();
        if(!ih->Load(argv[3])) {
            std::cout << "local vpk " << argv[3] << " load fail." << std::endl;
            delete ih;
            return 0;
        }
        ph = ih;
    } else {
        show_usage(argv[0]);
        return 0;
    }
    
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int res = connect(sock, (sockaddr*)&addr, sizeof(addr));
    if(res == 0) {
        char recv_buffer[8192];
        int recv_offset = 0;
        bool quit = false;
        std::cout << "server connected." << std::endl;
        LocalSender sender(sock);
        pkt_base hdr;
        // first packet
        ph->InitSend(sender);
        
        // begin recv
        while (!quit) {
            int recv_size = recv(sock, &recv_buffer[recv_offset], 8192 - recv_offset, 0);
            if (recv_size > 0) {
                recv_offset += recv_size;
                int offset = 0;
                while(offset + 4 <= recv_offset) {
                    int left_data_size = recv_offset - offset;  // include header
                    memcpy(&hdr, &recv_buffer[offset], 4);
                    if(hdr.length < 4 || hdr.length > 1500) {
                        // packet length error, skip
                        offset += 2;
                        continue;
                    };
                    if(hdr.length > left_data_size) {
                        // need receive more data
                        break;
                    }
                    if(ph->HandlePacket(sender, hdr.type, &recv_buffer[offset + 4], hdr.length - 4)) {
                        quit = true;
                        break;
                    }
                    offset += hdr.length;
                }
                if(offset != recv_offset)
                    memmove(recv_buffer, &recv_buffer[offset], recv_offset - offset);
                recv_offset -= offset;
            } else {
                // =0 -- connection closed
                // <0 -- error
                break;
            }
        }
    }
    close(sock);
    delete ph;
    return 0;
}
