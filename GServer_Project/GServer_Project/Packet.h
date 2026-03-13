#pragma once
#include <memory>
#include <cstdint>
#include <cstring>
#include <vector>

// -----------------------------------------------------------------------
// Packet
//   FSCore의 Packet 구조를 참고한 경량 네트워크 패킷 래퍼.
//   IOCP worker에서 수신한 raw 데이터를 감싸 Service로 전달한다.
//
//   메모리 레이아웃 (수신 버퍼):
//     [4바이트 messageID (int32 LE)][4바이트 fb_size][fb_size바이트 FB 데이터]
//   Packet은 이미 파싱된 fb_data(포인터) + fb_size 만 보관한다.
// -----------------------------------------------------------------------
class Packet
{
public:
    using SharedPtr = std::shared_ptr<Packet>;

    int     HostID      = 0;   // 송신 클라이언트 ID
    int     MessageID   = 0;   // EPacketProtocol 값

private:
    std::vector<uint8_t> mBuffer;  // FlatBuffers 페이로드 복사본

public:
    Packet() = default;
    ~Packet() = default;

    // -----------------------------------------------------------------------
    // 수신 데이터로 Packet 생성
    // -----------------------------------------------------------------------
    static SharedPtr New(int hostID, int messageID, const uint8_t* pData, uint32_t dataSize)
    {
        auto pPacket        = std::make_shared<Packet>();
        pPacket->HostID     = hostID;
        pPacket->MessageID  = messageID;
        pPacket->mBuffer.assign(pData, pData + dataSize);
        return pPacket;
    }

    // -----------------------------------------------------------------------
    // Accessors (FSCore Packet 인터페이스와 동일하게 맞춤)
    // -----------------------------------------------------------------------
    const void* GetDataPtr()    const { return mBuffer.data(); }
    int         GetMessageID()  const { return MessageID; }
    size_t      GetMessageSize()const { return mBuffer.size(); }
};
