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
        cout << "generate_tfmovr v1.0 by IkotasUsage: generate_tfmovr.exe file" << endl;
        return 1;
    }

    fs::path sourcePath(argv[1]);
    string baseName = sourcePath.stem().string();
    fs::path dir = sourcePath.parent_path();
    fs::path tfmPath = dir / (baseName + ".tfm");
    fs::path qpPath = dir / (baseName + ".qp");
    fs::path outPath = dir / (baseName + ".tfmovr");

    if (!fs::exists(tfmPath)) { cerr << "Error: " << tfmPath.filename().string() << " not found." << endl; return 1; }
    if (!fs::exists(qpPath)) { cerr << "Error: " << qpPath.filename().string() << " not found." << endl; return 1; }

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
            if (fIdx >= (int)frames.size()) frames.resize((size_t)fIdx + 5, ' ');
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
            // ロジック 1
            int numG;
            auto [best, maxC] = GetMostFrequentPattern(currentSC, nextSC, frames, numG);
            SceneResult res = { currentSC, "c", {} };
            if (maxC > 0) { res.pattern = best; res.identity = GetIdentity(best, currentSC); }
            finalResults.push_back(res);
            lastValidIdentity = res.identity; lastAppliedOrigin = currentSC;
        }
        else {
            // ロジック 2-2: 同一性チェック
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

            // ロジック 2-3: 差分90以上でロジック3へ
            if (currentSC - lastAppliedOrigin >= 90) {
                bool overlap32 = false;
                for (int g = 0; g < 10; ++g) {
                    size_t fs_idx = (size_t)currentSC - 50 + (size_t)g * 5;
                    string seg = ""; for (int k = 0; k < 5; ++k) seg += frames[fs_idx + k];
                    for (const auto& pat : VALID_PATTERNS) {
                        if (seg == pat && GetIdentity(pat, (int)fs_idx) == lastValidIdentity) { overlap32 = true; break; }
                    }
                }

                if (!overlap32) {
                    // ロジック 3-3, 3-4, 3-5
                    int numG33;
                    auto [recordedPat, recC] = GetMostFrequentPattern(currentSC - 50, currentSC, frames, numG33);
                    int offset = -100;
                    bool backtrackProcessed = false;

                    while (currentSC + offset >= lastAppliedOrigin) {
                        bool found35 = false;
                        for (int g = 0; g < 10; ++g) {
                            size_t fs_idx = (size_t)currentSC + offset + (size_t)g * 5;
                            string seg = ""; for (int k = 0; k < 5; ++k) seg += frames[fs_idx + k];
                            for (const auto& pat : VALID_PATTERNS) {
                                if (seg == pat && GetIdentity(pat, (int)fs_idx) == lastValidIdentity) { found35 = true; break; }
                            }
                        }
                        if (found35) {
                            // 発生起点の特定: recordedPat が最初に出現したフレームを探す
                            int backtrackOrigin = currentSC + offset;
                            for (int f = currentSC + offset; f < currentSC; ++f) {
                                string seg = ""; for (int k = 0; k < 5; ++k) seg += frames[(size_t)f + k];
                                if (seg == recordedPat) { backtrackOrigin = f; break; }
                            }
                            // Logic 1 を適用して追加
                            int nG; auto [bPat, mC] = GetMostFrequentPattern(backtrackOrigin, currentSC, frames, nG);
                            SceneResult sr = { backtrackOrigin, bPat, GetIdentity(bPat, backtrackOrigin) };
                            sr.comment = "# " + to_string(backtrackOrigin) + " <- This frame is shifted forward.";
                            finalResults.push_back(sr);
                            lastValidIdentity = sr.identity; lastAppliedOrigin = backtrackOrigin;

                            // 重要: 今回の SC を再度評価するためインデックスを戻す
                            i--;
                            backtrackProcessed = true;
                            break;
                        }
                        offset -= 50;
                    }
                    if (backtrackProcessed) continue;
                }
            }

            // ロジック 2-4: -1フレームチェック
            int startPoint = currentSC;
            int fM1 = currentSC - 1;
            if (fM1 >= 0) {
                string segM1 = ""; for (int k = 0; k < 5; ++k) segM1 += frames[(size_t)fM1 + k];
                if (segM1 == "pcccp" || segM1 == "ppccc") {
                    if (!lastValidIdentity.p_pos[fM1 % 5]) startPoint = fM1;
                }
            }

            int finalNumG;
            auto [finalPat, finalC] = GetMostFrequentPattern(startPoint, nextSC, frames, finalNumG);
            if (finalC >= (finalNumG + 3) / 4) {
                SceneResult sr = { startPoint, finalPat, GetIdentity(finalPat, startPoint) };
                finalResults.push_back(sr);
                lastValidIdentity = sr.identity; lastAppliedOrigin = startPoint;
            }
            else if (i == (int)sceneChanges.size() - 1) {
                finalResults.push_back({ currentSC, "c", {} });
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
