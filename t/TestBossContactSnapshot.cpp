/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "DcBossContactSnapshot.h"

using namespace DcBossContactSnapshot;

namespace
{
    // The shape of the first Emperor contact (run 1823401): the tank 14.6yd from
    // him, nobody else fighting, the gate last read "party not ready".
    Inputs EmperorContact()
    {
        Inputs in;
        in.contactName = "Zug";
        in.bossName = "Emperor Dagran Thaurissan";
        in.contactDistToBoss = 14.6f;
        in.targetName = "Twilight's Hammer Ambassador";
        in.targetDist = 3.2f;
        in.roomTrashLeft = 35;
        in.roomGaveUp = true;
        in.lastGate = "party not ready";
        in.pullPhase = 0;
        in.members.push_back({"Zug", 80, -1, false, true, 14.6f});
        in.members.push_back({"Uzza", 100, 53, false, true, 25.1f});
        return in;
    }
}

TEST(BossContactSnapshot, NamesEveryFieldTheEmperorQuestionNeeds)
{
    std::string const line = Format(EmperorContact());
    EXPECT_NE(line.find("[DC:Zug] boss contact snapshot: Emperor Dagran Thaurissan at 14.6yd"),
              std::string::npos) << line;
    EXPECT_NE(line.find("target Twilight's Hammer Ambassador 3.2yd"), std::string::npos) << line;
    EXPECT_NE(line.find("room trash 35 left, room-clear gave up"), std::string::npos) << line;
    EXPECT_NE(line.find("last gate party not ready"), std::string::npos) << line;
    EXPECT_NE(line.find("pull phase 0"), std::string::npos) << line;
    EXPECT_NE(line.find("Uzza hp 100% mana 53% idle 25.1yd;"), std::string::npos) << line;
}

TEST(BossContactSnapshot, AMemberWithoutAManaBarSaysNone)
{
    std::string const line = Format(EmperorContact());
    EXPECT_NE(line.find("Zug hp 80% mana none idle 14.6yd;"), std::string::npos) << line;
}

TEST(BossContactSnapshot, NoTargetNoGateAndStillGoingRead)
{
    Inputs in = EmperorContact();
    in.targetName.clear();
    in.lastGate.clear();
    in.roomGaveUp = false;
    std::string const line = Format(in);
    EXPECT_NE(line.find("target none"), std::string::npos) << line;
    EXPECT_NE(line.find("last gate none"), std::string::npos) << line;
    EXPECT_NE(line.find("room-clear still going"), std::string::npos) << line;
}

TEST(BossContactSnapshot, ADeadMemberCarriesNoBars)
{
    Inputs in = EmperorContact();
    in.members[1].alive = false;
    std::string const line = Format(in);
    EXPECT_NE(line.find("Uzza dead"), std::string::npos) << line;
    EXPECT_EQ(line.find("Uzza hp"), std::string::npos) << line;
}

TEST(BossContactSnapshot, AFightingMemberSaysFighting)
{
    Inputs in = EmperorContact();
    in.members[0].inCombat = true;
    EXPECT_NE(Format(in).find("Zug hp 80% mana none fighting 14.6yd;"), std::string::npos);
}
