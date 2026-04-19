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
    string comment = "";
};

static pair<string, int> GetMostFrequentPattern(int start, int end, const vector<char>& frames, int& numGroupsOut) {
    if (start < 0) start = 0;
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
    if (argc < 2) {
        cout << "---------------------------" << endl
             << "generate_tfm v1.0 by Ikotas" << endl
             << "---------------------------" << endl
             << "Usage: generate_tfm.exe file" << endl;
        return 1;
    }

    fs::path sourcePath(argv[1]);
    string baseName = sourcePath.stem().string();
    fs::path dir = sourcePath.parent_path();
    fs::path tfmPath = dir / (baseName + ".tfm");
    fs::path qpPath = dir / (baseName + ".qp");
    fs::path outPath = dir / (baseName + ".tfmovr");

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

    // 1. TFM解析
    vector<char> frames;
    ifstream tfmFile(tfmPath);
    string line;
    for (int i = 0; i < 3; ++i) getline(tfmFile, line);
    while (getline(tfmFile, line)) {
        if (line.empty() || line[0] == '#') continue;
        stringstream ss(line);
        int fIdx; char type; string score;
        while (ss >> fIdx >> type >> score) {
            if (type == 'h') type = 'p';
            if (fIdx >= (int)frames.size()) frames.resize((size_t)fIdx + 10, ' ');
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
    int lastAppliedOrigin = -1;
    CycleIdentity lastValidIdentity;

    for (int i = 0; i < (int)sceneChanges.size(); ++i) {
        int currentSC = sceneChanges[i];
        int nextSC = (i + 1 < (int)sceneChanges.size()) ? sceneChanges[i + 1] : (int)frames.size();
        if (i > 0 && currentSC - sceneChanges[i - 1] < 10) continue;

        if (lastAppliedOrigin == -1) {
            int numG;
            auto [best, maxC] = GetMostFrequentPattern(currentSC, nextSC, frames, numG);
            SceneResult res = { currentSC, (maxC > 0 ? best : "c"), (maxC > 0 ? GetIdentity(best, currentSC) : CycleIdentity{}) };
            finalResults.push_back(res);
            lastValidIdentity = res.identity; lastAppliedOrigin = currentSC;
        }
        else {
            // ロジック 2-2: 同一性チェック または 基本パターンなしでスキップ [Source 114]
            int numG22; GetMostFrequentPattern(currentSC, nextSC, frames, numG22);
            bool identityFound = false;
            bool anyValidPattern = false;
            for (int g = 0; g < numG22; ++g) {
                size_t fs_idx = (size_t)currentSC + (size_t)g * 5;
                if (fs_idx + 5 > frames.size()) break;
                string seg = ""; for (int k = 0; k < 5; ++k) seg += frames[fs_idx + k];
                bool isValid = false; for (const auto& p : VALID_PATTERNS) if (seg == p) { isValid = true; break; }
                if (isValid) {
                    anyValidPattern = true;
                    if (GetIdentity(seg, (int)fs_idx) == lastValidIdentity) { identityFound = true; break; }
                }
            }
            if (identityFound || !anyValidPattern) continue; // 2-2 更新箇所

            // ロジック 2-3 & 3: バックトラック
            bool backtrackHandled = false;
            if (currentSC - lastAppliedOrigin >= 90) {
                // 3-1, 3-2 (更新) [Source 114]
                bool overlap32 = false; int validCount32 = 0;
                for (int g = 0; g < 10; ++g) {
                    size_t fs_idx = (size_t)currentSC - 50 + (size_t)g * 5;
                    if (fs_idx + 5 > frames.size()) break;
                    string seg = ""; for (int k = 0; k < 5; ++k) seg += frames[fs_idx + k];
                    bool isValid = false;
                    for (const auto& pat : VALID_PATTERNS) {
                        if (seg == pat) {
                            isValid = true;
                            if (GetIdentity(pat, (int)fs_idx) == lastValidIdentity) overlap32 = true;
                            break;
                        }
                    }
                    if (isValid) validCount32++;
                }

                if (!overlap32 && validCount32 > 3) {
                    // 3-3, 3-4, 3-5
                    int numG33; auto [recordedPat, recC] = GetMostFrequentPattern(currentSC - 50, currentSC, frames, numG33);
                    if (recordedPat != "") {
                        CycleIdentity targetID = GetIdentity(recordedPat, currentSC - 50);
                        int offset = -100;
                        while (currentSC + offset >= lastAppliedOrigin) {
                            bool foundPrevCycle = false;
                            for (int g = 0; g < 10; ++g) {
                                size_t fs_idx = (size_t)currentSC + offset + (size_t)g * 5;
                                if (fs_idx + 5 > frames.size()) break;
                                string seg = ""; for (int k = 0; k < 5; ++k) seg += frames[fs_idx + k];
                                for (const auto& pat : VALID_PATTERNS) if (seg == pat && GetIdentity(pat, (int)fs_idx) == lastValidIdentity) { foundPrevCycle = true; break; }
                            }
                            if (foundPrevCycle) {
                                // 3-5: 発生起点の正確な特定
                                int backtrackOrigin = -1;
                                string originPattern = "";
                                for (int f = currentSC + offset; f < currentSC; ++f) {
                                    if ((size_t)f + 5 > frames.size()) break;
                                    string seg = ""; for (int k = 0; k < 5; ++k) seg += frames[(size_t)f + k];

                                    string matchedPat = "";
                                    for (const auto& p : VALID_PATTERNS) if (seg == p) { matchedPat = p; break; }

                                    // 修正点: recordedPat 文字列ではなく、targetID (mod 5 同一性) で判定 [Source 8]
                                    if (!matchedPat.empty() && GetIdentity(matchedPat, f) == targetID) {
                                        if (lastValidIdentity.has_p && lastValidIdentity.p_pos[f % 5]) continue;
                                        bool oldCycleRemains = false;
                                        for (int checkF = f + 1; checkF < currentSC; ++checkF) {
                                            if ((size_t)checkF + 5 > frames.size()) break;
                                            string sC = ""; for (int k = 0; k < 5; ++k) sC += frames[(size_t)checkF + k];
                                            for (const auto& p : VALID_PATTERNS) if (sC == p && GetIdentity(p, checkF) == lastValidIdentity) { oldCycleRemains = true; break; }
                                            if (oldCycleRemains) break;
                                        }
                                        if (!oldCycleRemains) { backtrackOrigin = f; originPattern = matchedPat; break; }
                                    }
                                }
                                if (backtrackOrigin != -1) {
                                    SceneResult sr = { backtrackOrigin, originPattern, GetIdentity(originPattern, backtrackOrigin) };
                                    sr.comment = "# " + to_string(backtrackOrigin) + " <- " + to_string(currentSC) + "  Moved forward to detect cycle change.";
                                    finalResults.push_back(sr);
                                    lastValidIdentity = sr.identity; lastAppliedOrigin = backtrackOrigin;
                                    i--; backtrackHandled = true;
                                }
                                break;
                            }
                            offset -= 50;
                        }
                    }
                }
            }
            if (backtrackHandled) continue;

            // ロジック 2-4
            int startPoint = currentSC;
            int fM1 = currentSC - 1;
            if (fM1 >= 0 && (size_t)fM1 + 5 <= frames.size()) {
                string segM1 = ""; for (int k = 0; k < 5; ++k) segM1 += frames[(size_t)fM1 + k];
                if (segM1 == "pcccp" || segM1 == "ppccc") {
                    if (!lastValidIdentity.p_pos[fM1 % 5]) startPoint = fM1;
                }
            }

            // ロジック 1-2a & 1-2b (更新) [Source 109]
            int finalNumG; auto [finalPat, finalC] = GetMostFrequentPattern(startPoint, nextSC, frames, finalNumG);
            if (finalC >= (finalNumG + 3) / 4) {
                SceneResult sr = { startPoint, finalPat, GetIdentity(finalPat, startPoint) };
                finalResults.push_back(sr);
                lastValidIdentity = sr.identity; lastAppliedOrigin = startPoint;
            }
            else {
                // 2番目以降（最終含む）でパターンが見当たらない場合はスキップ [Source 109]
                // 何もしない（出力リストに追加しない）
            }
        }
    }

    // 4. 出力
    ofstream outFile(outPath);
    for (size_t idx = 0; idx < finalResults.size(); ++idx) {
        outFile << finalResults[idx].startFrame << ",";
        if (idx + 1 < finalResults.size()) outFile << (finalResults[idx + 1].startFrame - 1);
        else outFile << "0";
        outFile << " " << finalResults[idx].pattern << "\n";
        if (!finalResults[idx].comment.empty()) outFile << finalResults[idx].comment << "\n";
    }
    outFile.close();
    cout << "Successfully generated: " << baseName << ".tfmovr" << endl;
    return 0;
}
