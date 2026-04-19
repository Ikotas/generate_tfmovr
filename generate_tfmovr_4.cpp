#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <map>
#include <filesystem>
#include <array>

using namespace std;
namespace fs = std::filesystem;

// 厳密な5種類の基本パターン [2]
const vector<string> VALID_PATTERNS = { "cccpp", "pcccp", "ppccc", "cppcc", "ccppc" };

struct CycleIdentity {
    std::array<bool, 5> p_pos = { false, false, false, false, false };
    bool is_c = true;
    bool operator==(const CycleIdentity& other) const {
        if (is_c && other.is_c) return true;
        if (is_c != other.is_c) return false;
        return p_pos == other.p_pos;
    }
    bool operator!=(const CycleIdentity& other) const { return !(*this == other); }
};

static CycleIdentity GetIdentity(const string& pattern, int startFrame) {
    CycleIdentity id;
    if (pattern == "c" || pattern.length() < 5) { id.is_c = true; return id; }
    id.is_c = false;
    for (int i = 0; i < 5; ++i) {
        if (pattern[i] == 'p') id.p_pos[(static_cast<size_t>(startFrame) + i) % 5] = true;
    }
    return id;
}

static string GetRepresentativePattern(int start, const vector<char>& frames, CycleIdentity targetID) {
    map<string, int> counts;
    for (int g = 0; g < 10; ++g) {
        size_t idx = (size_t)start + g * 5;
        if (idx + 5 > frames.size()) break;
        string seg(frames.begin() + idx, frames.begin() + idx + 5);
        if (GetIdentity(seg, (int)idx) == targetID) {
            for (const auto& p : VALID_PATTERNS) if (seg == p) { counts[p]++; break; }
        }
    }
    string best = ""; int maxC = 0;
    for (auto const& [pat, c] : counts) if (c > maxC) { maxC = c; best = pat; }
    if (best.empty()) {
        for (const auto& p : VALID_PATTERNS) if (GetIdentity(p, start) == targetID) return p;
        return "c";
    }
    return best;
}

static bool IsStable(int start, const vector<char>& frames, CycleIdentity targetID) {
    int matchCount = 0;
    for (int g = 0; g < 10; ++g) {
        size_t idx = (size_t)start + g * 5;
        if (idx + 5 > frames.size()) break;
        string seg(frames.begin() + idx, frames.begin() + idx + 5);
        if (GetIdentity(seg, (int)idx) == targetID) matchCount++;
    }
    return matchCount >= 5;
}

static bool IsProgressiveSection(int start, const vector<char>& frames) {
    int cCount = 0, range = 150;
    if (start + range > (int)frames.size()) range = (int)frames.size() - start;
    if (range < 20) return false;
    for (int i = 0; i < range; ++i) if (frames[start + i] == 'c') cCount++;
    return (cCount * 100 / range) >= 95;
}

struct SceneResult { int frame; string pattern; CycleIdentity id; };

int main(int argc, char* argv[]) {
    // メッセージの刷新 [3]
    if (argc < 2) {
        cout << "------------------------------" << endl;
        cout << "generate_tfmovr v1.0 by Ikotas" << endl;
        cout << "------------------------------" << endl;
        cout << "Usage: generate_tfmovr.exe TFM_output_file(*.tfm)" << endl << endl;
        cout << "The output of TFM is as follows:" << endl;
        cout << "TFM(mode=0,pp=1,slow=2,micmatching=0,output=\"x:\\path\\filename.tfm\")" << endl;
        return 1;
    }

    fs::path tfmP = argv[1]; if (tfmP.extension() != ".tfm") tfmP.replace_extension(".tfm");
    if (!fs::exists(tfmP)) {
        cerr << "Error: " << tfmP.filename().string() << " not found." << endl;
        cerr << "Make sure the output file for TFM is in the same folder." << endl << endl;
        cerr << "The output of TFM is as follows:" << endl;
        cerr << "TFM(mode=0,pp=1,slow=2,micmatching=0,output=\"x:\\path\\filename.tfm\")" << endl;
        return 1;
    }

    vector<char> frames; ifstream tfmFile(tfmP); string line;
    for (int i = 0; i < 3; ++i) getline(tfmFile, line);
    while (getline(tfmFile, line)) {
        if (line.empty() || line[0] == '#') continue;
        stringstream ss(line); int fIdx; string typeStr;
        if (!(ss >> fIdx >> typeStr)) continue;
        char type = typeStr[0]; if (type == 'h') type = 'p';
        if (fIdx >= (int)frames.size()) frames.resize(fIdx + 1, ' ');
        frames[fIdx] = type;
    }

    vector<SceneResult> results;
    CycleIdentity lastID;
    int currentFrame = 0;

    // 初回起点設定 [2]
    {
        map<string, int> counts;
        for (int g = 0; g < 10 && g * 5 + 5 <= (int)frames.size(); ++g) {
            string seg(frames.begin() + g * 5, frames.begin() + g * 5 + 5);
            for (const auto& p : VALID_PATTERNS) if (seg == p) counts[p]++;
        }
        string initPat = "c"; int maxC = 0;
        for (auto const& [p, c] : counts) if (c > maxC) { maxC = c; initPat = p; }
        results.push_back({ 0, initPat, GetIdentity(initPat, 0) });
        lastID = results.back().id; currentFrame = 1;
    }

    while (currentFrame + 5 <= (int)frames.size()) {
        string seg(frames.begin() + currentFrame, frames.begin() + currentFrame + 5);
        CycleIdentity currentID = GetIdentity(seg, currentFrame);

        if (currentID != lastID) {
            bool transitionFound = false;
            if (!currentID.is_c && IsStable(currentFrame, frames, currentID)) {
                int x = currentFrame;
                while (x > 0 && x > currentFrame - 50) {
                    string bSeg(frames.begin() + x - 1, frames.begin() + x + 4);
                    if (GetIdentity(bSeg, x - 1) != currentID) break;
                    // 高精度衝突回避 [5]
                    if (!lastID.is_c && lastID.p_pos[(size_t)(x - 1) % 5] && frames[x - 1] == 'p') break;
                    x--;
                }
                while (x < (int)frames.size() && !lastID.is_c && lastID.p_pos[(size_t)x % 5] && frames[x] == 'p') {
                    x++;
                }
                string finalPat = GetRepresentativePattern(x, frames, currentID);
                if (finalPat != "c") {
                    results.push_back({ x, finalPat, GetIdentity(finalPat, x) });
                    lastID = results.back().id; currentFrame = x + 10; transitionFound = true;
                }
            }
            else if (IsProgressiveSection(currentFrame, frames)) {
                int x = currentFrame;
                if (!lastID.is_c) {
                    for (int f = max(0, currentFrame - 10); f <= currentFrame + 10; ++f) {
                        if (lastID.p_pos[(size_t)f % 5] && frames[f] == 'c') { x = f; break; }
                    }
                }
                results.push_back({ x, "c", GetIdentity("c", x) });
                lastID = results.back().id; currentFrame = x + 150; transitionFound = true;
            }
            if (transitionFound) continue;
        }
        currentFrame++;
    }

    // tfmovr 出力形式の適用 [1]
    fs::path outP = tfmP; outP.replace_extension(".tfmovr");
    ofstream outFile(outP);
    for (size_t i = 0; i < results.size(); ++i) {
        int start = results[i].frame;
        int end = (i + 1 < results.size()) ? results[i + 1].frame - 1 : 0;
        outFile << start << "," << end << " " << results[i].pattern << endl;
    }

    cout << "Successfully generated: " << outP.filename().string() << " (" << results.size() << " entries)" << endl;

    return 0;
}
