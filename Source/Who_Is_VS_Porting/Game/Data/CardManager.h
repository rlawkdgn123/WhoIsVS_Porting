#pragma once

// YunoGame/System/CardManager 이식 (인터페이스 동일)

#include <string>
#include <unordered_map>

#include "CardData.h"

class CardManager
{
public:
    bool LoadFromCSV(const std::string& path);

    const CardData& GetCardData(int cardID) const;
    const CardMoveData* GetMoveData(int cardID) const;
    const CardEffectData* GetEffectData(int cardID) const;

    size_t GetCardCount() const { return m_cardData.size(); } // UE 포팅 시 추가

private:
    std::unordered_map<int, CardData>       m_cardData;
    std::unordered_map<int, CardMoveData>   m_moveData;
    std::unordered_map<int, CardEffectData> m_effectData;
};
