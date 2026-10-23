#include "Packet.hpp"
#include "TestUtil.hpp"

using namespace lumilink;

int main()
{
  // Orders
  CHECK_EQ(Packet::Encode(Command::MakeColor(3, Color{0xFF, 0x80, 0x00})),
           (Bytes{'L', 1, 5, 'C', 3, 0xFF, 0x80, 0x00}));
  CHECK_EQ(Packet::Encode(Command::MakeBrightness(kAllLeds, 128)),
           (Bytes{'L', 1, 3, 'B', 0xFF, 128}));
  CHECK_EQ(Packet::Encode(Command::MakeEffect(7, Effect::Breathe, 20, Color{0, 0, 255})),
           (Bytes{'L', 1, 7, 'E', 7, 2, 20, 0, 0, 255}));
  CHECK_EQ(Packet::Encode(Command::MakeTurnOff(0)), (Bytes{'L', 1, 2, 'O', 0}));

  // Zero bytes are kept (packets are binary, not C strings)
  CHECK_EQ(Packet::Encode(Command::MakeColor(0, Color{0, 0, 0})).size(), 8u);

  // Handshake
  CHECK_EQ(Packet::Handshake(1, 3), (Bytes{'H', 1, 3}));

  const std::uint8_t ok[] = {'H', 2};
  const std::uint8_t tooNew[] = {'H', 4};
  const std::uint8_t wrongTag[] = {'X', 1};
  CHECK_EQ(Packet::HandshakeResult(ok, 2, 1, 3), 2);
  CHECK_EQ(Packet::HandshakeResult(tooNew, 2, 1, 3), 0);
  CHECK_EQ(Packet::HandshakeResult(wrongTag, 2, 1, 3), 0);
  CHECK_EQ(Packet::HandshakeResult(ok, 1, 1, 3), 0);
  CHECK_EQ(Packet::HandshakeResult(nullptr, 2, 1, 3), 0);

  // Formatting
  CHECK_EQ(Packet::ToHex(Bytes{0x4C, 0x01, 0xAB}), std::string("4C 01 AB"));
  CHECK_EQ(Packet::ToHex(Bytes{}), std::string());
  CHECK_EQ(Describe(Command::MakeColor(3, Color{0xFF, 0x80, 0x00})), std::string("LED 3 color #FF8000"));
  CHECK_EQ(Describe(Command::MakeTurnOff(kAllLeds)), std::string("all LEDs off"));

  return lumilink_test::Finish("packet_tests");
}
