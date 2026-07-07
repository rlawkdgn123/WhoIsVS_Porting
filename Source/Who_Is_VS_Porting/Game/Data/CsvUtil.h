#pragma once

// CardManager/CardRangeManager 공용 CSV 유틸.
// 원본은 각 cpp의 static 함수였으나 UE 유니티 빌드에서 중복 정의되어 헤더로 추출.

#include <sstream>
#include <string>
#include <vector>

namespace yunocsv
{
    // CSV 한 줄 분리 유틸
    inline std::vector<std::string> Split(const std::string& line, char delim)
    {
        std::vector<std::string> tokens;
        std::stringstream ss(line);
        std::string item;

        while (std::getline(ss, item, delim))
        {
            tokens.push_back(item);
        }
        return tokens;
    }

    // BOM 제거 유틸
    inline void RemoveBOM(std::string& s)
    {
        if (s.size() >= 3 &&
            (unsigned char)s[0] == 0xEF &&
            (unsigned char)s[1] == 0xBB &&
            (unsigned char)s[2] == 0xBF)
        {
            s.erase(0, 3);
        }
    }
}
