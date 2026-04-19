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

// 厳密な5種類の基本パターン
const vector<string> VALID_PATTERNS = { "cccpp", "pcccp", "ppccc", "cppcc", "ccppc" };

struct CycleIdentity {
    std::array<bool, 5> p_pos = { false, false, false, false, false };
    bool has_p = false;
    bool operator==(const CycleIdentity& other) const { return this->p_pos == other.p_pos; }
};

static CycleIdentity GetIdentity(const string& pattern, int startFrame) {
    CycleIdentity id;
    for (int i = 0; i < 5; ++i) {
        if (pattern[i] == 'p') {
            id.p_pos[(static_cast<size_t>(startFrame) + i) % 5] = true;
            id.has_p = true;
        }
    }
    return id;
}

struct SceneResult {
    int startFrame;
    string pattern;
    CycleIdentity identity;
};

static pair<string, int> GetMostFrequentPattern(int start, int end, const vector<char>& frames, int& numGroupsOut) {
    int gap = end - start;
    int numGroups = (gap >= 50) ? 10 : (gap > 0 ? gap / 5 : 0);
    numGroupsOut = numGroups;
    if (numGroups == 0) return { "", 0 };
    map<string, int> counts;
    for (int g = 0; g < numGroups; ++g) {
        size_t fs_idx = (size_t)start + (size_t)g * 5;
        if (fs_idx + 5 > frames.size()) break;
        string seg = "";
        for (int k = 0; k < 5; ++k) seg += frames[fs_idx + k];
        for (const auto& pat : VALID_PATTERNS) if (seg == pat) { counts[pat]++; break; }
    }
    string best = ""; int maxC = 0;
    for (auto const& [pat, c] : counts) if (c > maxC) { maxC = c; best = pat; }
    return { best, maxC };
}

int main(int argc, char* argv[]) {
    // 起動メッセージ [Source 16]
    if (argc < 2) {
        cout << "generate_tfmovr v1.0 by IkotasUsage: generate_tfmovr.exe file" << endl;
        return 1;
    }

    fs::path sourcePath(argv[1]);
    string baseName = sourcePath.stem().string();
    fs::path dir = sourcePath.parent_path();
    fs::path tfmPath = dir / (baseName + ".tfm");
    fs::path qpPath = dir / (baseName + ".qp");
    fs::path outPath = dir / (baseName + ".tfmovr");

    // --- 追加箇所: ファイル存在チェック ---
    if (!fs::exists(tfmPath)) {
        cerr << "Error: " << tfmPath.filename().string() << " not found." << endl
            << "Make sure the output file for TFM is in the same folder." << endl;
        return 1;
    }
    if (!fs::exists(qpPath)) {
        cerr << "Error: " << qpPath.filename().string() << " not found." << endl
            << "Make sure the output file for PySceneDetect is in the same folder." << endl;
        return 1;
    }
    // ------------------------------------

    // 1. TFM解析
    vector<char> frames;
    ifstream tfmFile(tfmPath);
    if (!tfmFile.is_open()) return 1;
    string line;
    for (int i = 0; i < 3; ++i) getline(tfmFile, line);
    while (getline(tfmFile, line)) {
        if (line.empty() || line[0] == '#') continue; // [Source 16]
        stringstream ss(line);
        int fIdx; char type; string score;
        while (ss >> fIdx >> type >> score) {
            if (type == 'h') type = 'p';
            if (fIdx >= (int)frames.size()) frames.resize((size_t)fIdx + 1, ' ');
            frames[(size_t)fIdx] = type;
        }
    }

    // 2. QP解析
    vector<int> sceneChanges;
    ifstream qpFile(qpPath);
    while (getline(qpFile, line)) {
        stringstream ss(line); int f;
        if (ss >> f) sceneChanges.push_back(f);
    }

    // 3. メインロジック
    vector<SceneResult> finalResults;
    int lastAppliedSC = -1;
    CycleIdentity lastValidIdentity;

    for (size_t i = 0; i < sceneChanges.size(); ++i) {
        int currentSC = sceneChanges[i];
        int nextSC = (i + 1 < sceneChanges.size()) ? sceneChanges[i + 1] : (int)frames.size();
        if (i > 0 && currentSC - sceneChanges[i - 1] < 10) continue;

        if (lastAppliedSC == -1) {
            int numG;
            auto [best, maxC] = GetMostFrequentPattern(currentSC, nextSC, frames, numG);
            SceneResult res = { currentSC, "c", {} };
            if (maxC > 0) { res.pattern = best; res.identity = GetIdentity(best, currentSC); }
            finalResults.push_back(res);
            lastValidIdentity = res.identity; lastAppliedSC = currentSC;
        }
        else {
            int numG22;
            GetMostFrequentPattern(currentSC, nextSC, frames, numG22);
            bool identityFound = false;
            for (int g = 0; g < numG22; ++g) {
                size_t fs_idx = (size_t)currentSC + (size_t)g * 5;
                string seg = ""; for (int k = 0; k < 5; ++k) seg += frames[fs_idx + k];
                for (const auto& pat : VALID_PATTERNS) {
                    if (seg == pat && GetIdentity(pat, (int)fs_idx) == lastValidIdentity) { identityFound = true; break; }
                }
                if (identityFound) break;
            }
            if (identityFound) continue;

            int startPoint = currentSC;
            if (currentSC - lastAppliedSC >= 90) {
                bool overlap32 = false;
                for (int g = 0; g < 10; ++g) {
                    size_t fs_idx = (size_t)currentSC - 50 + (size_t)g * 5;
                    if (fs_idx + 5 > frames.size()) break;
                    string seg = ""; for (int k = 0; k < 5; ++k) seg += frames[fs_idx + k];
                    for (const auto& pat : VALID_PATTERNS) {
                        if (seg == pat && GetIdentity(pat, (int)fs_idx) == lastValidIdentity) { overlap32 = true; break; }
                    }
                }
                if (!overlap32) {
                    int numG33;
                    auto [recordedPat, recC] = GetMostFrequentPattern(currentSC - 50, currentSC, frames, numG33);
                    int offset = -100;
                    while (currentSC + offset >= lastAppliedSC) {
                        bool found35 = false;
                        for (int g = 0; g < 10; ++g) {
                            size_t fs_idx = (size_t)currentSC + offset + (size_t)g * 5;
                            if (fs_idx + 5 > frames.size()) break;
                            string seg = ""; for (int k = 0; k < 5; ++k) seg += frames[fs_idx + k];
                            for (const auto& pat : VALID_PATTERNS) {
                                if (seg == pat && GetIdentity(pat, (int)fs_idx) == lastValidIdentity) { found35 = true; break; }
                            }
                        }
                        if (found35) {
                            for (int f = currentSC + offset; f < currentSC; ++f) {
                                size_t f_idx = (size_t)f;
                                if (f_idx + 5 > frames.size()) break;
                                string seg = ""; for (int k = 0; k < 5; ++k) seg += frames[f_idx + k];
                                if (seg == recordedPat) { startPoint = f; break; }
                            }
                            break;
                        }
                        offset -= 50;
                    }
                }
            }
            if (startPoint == currentSC) {
                int fM1 = currentSC - 1;
                if (fM1 >= 0) {
                    string segM1 = ""; for (int k = 0; k < 5; ++k) segM1 += frames[(size_t)fM1 + k];
                    if (segM1 == "pcccp" || segM1 == "ppccc") {
                        if (lastValidIdentity.p_pos[fM1 % 5]) startPoint = currentSC;
                        else startPoint = fM1;
                    }
                }
            }
            int finalNumG;
            auto [finalPat, finalC] = GetMostFrequentPattern(startPoint, nextSC, frames, finalNumG);
            if (finalC >= (finalNumG + 3) / 4) {
                SceneResult sr = { startPoint, finalPat, GetIdentity(finalPat, startPoint) };
                finalResults.push_back(sr);
                lastValidIdentity = sr.identity; lastAppliedSC = currentSC;
            }
            else if (i == sceneChanges.size() - 1) {
                finalResults.push_back({ currentSC, "c", {} });
            }
        }
    }

    // 4. 出力フォーマット [Source 10]
    ofstream outFile(outPath);
    if (!outFile) return 1;
    for (size_t idx = 0; idx < finalResults.size(); ++idx) {
        outFile << finalResults[idx].startFrame << ",";
        if (idx + 1 < finalResults.size()) {
            outFile << (finalResults[idx + 1].startFrame - 1);
        }
        else {
            outFile << "0";
        }
        outFile << " " << finalResults[idx].pattern << "\n";
    }
    outFile.close();

    // 成功メッセージ [Source 16]
    cout << "Successfully generated: " << baseName << ".tfmovr" << endl;

    return 0;
}
