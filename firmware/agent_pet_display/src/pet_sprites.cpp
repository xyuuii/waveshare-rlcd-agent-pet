#include "pet_sprites.h"

namespace {

const PetSpriteFrame kCapySleepA{{"          ", "  .----.  ", " ( -__-)  ", " /(____)z ", "  `----'  "}};
const PetSpriteFrame kCapySleepB{{"          ", "  .----.  ", " ( -..-)  ", " /(____)o ", "  `----'  "}};
const PetSpriteFrame kCapyIdleA{{"  .----.  ", " / o  o \\ ", "(   --   )", " \\_====_/ ", "  /____\\  "}};
const PetSpriteFrame kCapyIdleB{{"  .----.  ", " / o  o \\ ", "(   ..   )", " \\_====_/ ", "  /____\\  "}};
const PetSpriteFrame kCapyBusyA{{"  .----.  ", " / ^  ^ \\ ", "(   --   )", " \\_====_/ ", "  /_/\\_\\  "}};
const PetSpriteFrame kCapyBusyB{{"    ?     ", "  .----.  ", " / ^  ^ \\ ", "(   ..   )", "  \\____/  "}};
const PetSpriteFrame kCapyAttentionA{{"  ^    ^  ", " /O____O\\ ", "(   !!   )", " \\_====_/ ", "  /____\\  "}};
const PetSpriteFrame kCapyAttentionB{{"  ^^  ^^  ", "/^O____O^\\", "(   !!   )", "  \\====/  ", "   /__/   "}};
const PetSpriteFrame kCapyCelebrateA{{"  .----.  ", " / ^  ^ \\ ", "(   WW   )", "  \\====/  ", "   /__/   "}};
const PetSpriteFrame kCapyCelebrateB{{"   \\__/   ", "  .----.  ", " / ^  ^ \\ ", "(   WW   )", "  \\____/  "}};
const PetSpriteFrame kCapyErrorA{{"  .----.  ", " / x  @ \\ ", "(   vv   )", "  \\====/  ", "   /__/   "}};
const PetSpriteFrame kCapyErrorB{{"  .----.  ", " / @  x \\ ", "(   ~~   )", "  \\====/  ", "   /__/   "}};

}  // namespace

const PetSpriteFrame& spriteForMode(PetMode mode, uint32_t tickMs) {
  const bool phase = ((tickMs / 500) % 2) == 0;
  switch (mode) {
    case PetMode::Sleep:
      return phase ? kCapySleepA : kCapySleepB;
    case PetMode::Busy:
      return phase ? kCapyBusyA : kCapyBusyB;
    case PetMode::Attention:
      return phase ? kCapyAttentionA : kCapyAttentionB;
    case PetMode::Celebrate:
      return phase ? kCapyCelebrateA : kCapyCelebrateB;
    case PetMode::Error:
      return phase ? kCapyErrorA : kCapyErrorB;
    case PetMode::Tired:
      return phase ? kCapySleepB : kCapySleepA;
    case PetMode::Charging:
      return phase ? kCapyBusyB : kCapyIdleA;
    case PetMode::Idle:
    default:
      return phase ? kCapyIdleA : kCapyIdleB;
  }
}

const char* activePetSpecies() {
  return "CAPYBARA";
}
