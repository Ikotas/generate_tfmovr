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
    if (pattern == "c") { id.is_c = true; return id; }
    id.is_c = false;
    for (size_t i = 0; i < 5; ++i) {
        if (pattern[i] == 'p') id.p_pos[(static_cast<size_t>(startFrame) + i) % 5] = true;
    }
    return id;
}

static bool MatchStrict(int start, const vector<char>& frames, const CycleIdentity& id) {
    if (id.is_c) return false;
    for (int i = 0; i < 5; ++i) {
        size_t idx = static_cast<size_t>(start) + i;
        if (idx >= frames.size()) return false;
        char d = frames[idx];
        if (d == 'h') continue;
        bool target_p = id.p_pos[idx % 5];
        if (target_p && d != 'p') return false;
        if (!target_p && d != 'c') return false;
    }
    return true;
}

static bool MatchCore(int start, const vector<char>& frames, const CycleIdentity& id) {
    if (id.is_c) return false;
    for (int i = 0; i < 5; ++i) {
        size_t idx = static_cast<size_t>(start) + i;
        if (idx >= frames.size()) return false;
        if (id.p_pos[idx % 5]) {
            if (frames[idx] == 'c') return false;
        }
    }
    return true;
}

static int CountStable(int start, const vector<char>& frames, const CycleIdentity& targetID, bool useStrict) {
    int count = 0;
    for (int g = 0; g < 10; ++g) {
        if (static_cast<size_t>(start) + static_cast<size_t>(g) * 5 + 5 > frames.size()) break;
        if (useStrict ? MatchStrict(start + g * 5, frames, targetID) : MatchCore(start + g * 5, frames, targetID)) count++;
    }
    return count;
}

static int CountConsecutive(int start, const vector<char>& frames, const CycleIdentity& targetID) {
    int count = 0;
    for (int g = 0; g < 10; ++g) {
        if (static_cast<size_t>(start) + static_cast<size_t>(g) * 5 + 5 > frames.size()) break;
        if (MatchStrict(start + g * 5, frames, targetID)) count++;
        else break;
    }
    return count;
}

static bool IsProgressiveSection(int start, const vector<char>& frames) {
    int cCount = 0, range = 150;
    if (static_cast<size_t>(start) + range > frames.size()) range = static_cast<int>(frames.size() - start);
    if (range < 50) return false;
    for (int i = 0; i < range; ++i) if (frames[static_cast<size_t>(start) + i] == 'c') cCount++;
    return (static_cast<long long>(cCount) * 100 / range) >= 95;
}

static void PrintUsage() {
    cout << "--------------------------------" << endl;
    cout << "generate_tfmovr v1.0.1 by Ikotas" << endl;
    cout << "--------------------------------" << endl;
    cout << "Usage: generate_tfmovr.exe [options] TFM_output_file(*.tfm)" << endl << endl;
    cout << "Options:" << endl;
    cout << "  -d <num>  dominantTh (Default: 6)" << endl;
    cout << "  -m <num>  maintainTh (Default: 3)" << endl;
    cout << "  -a <num>  adoptionTh (Default: 4)" << endl;
    cout << "  -c <num>  consecutTh (Default: 2)" << endl;
    cout << "  -o <file> output filename" << endl << endl;
    cout << "The output of TFM is as follows:" << endl;
    cout << "TFM(mode=0,pp=1,slow=2,micmatching=0,output=\"x:\\path\\filename.tfm\")" << endl;
}

int main(int argc, char* argv[]) {
    int dominantTh = 6, maintainTh = 3, adoptionTh = 4, consecutTh = 2;
    string tfmPathStr = "", outPathStr = "";

    if (argc < 2) { PrintUsage(); return 1; }

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "-d" && i + 1 < argc) dominantTh = stoi(argv[++i]);
        else if (arg == "-m" && i + 1 < argc) maintainTh = stoi(argv[++i]);
        else if (arg == "-a" && i + 1 < argc) adoptionTh = stoi(argv[++i]);
        else if (arg == "-c" && i + 1 < argc) consecutTh = stoi(argv[++i]);
        else if (arg == "-o" && i + 1 < argc) outPathStr = argv[++i];
        else if (arg[0] != '-') tfmPathStr = arg;
    }

    if (tfmPathStr.empty()) { PrintUsage(); return 1; }

    fs::path tfmP = tfmPathStr;
    if (tfmP.extension() != ".tfm") tfmP.replace_extension(".tfm");

    if (!fs::exists(tfmP)) {
        cout << "Error: " << tfmP.filename().string() << " not found." << endl;
        cout << "Make sure the output file for TFM is in the same folder." << endl << endl;
        cout << "The output of TFM is as follows:" << endl;
        cout << "TFM(mode=0,pp=1,slow=2,micmatching=0,output=\"x:\\path\\filename.tfm\")" << endl;
        return 1;
    }

    fs::path outP;
    if (outPathStr.empty()) {
        outP = tfmP; outP.replace_extension(".tfmovr");
    }
    else {
        outP = outPathStr;
        if (!outP.has_extension()) outP.replace_extension(".tfmovr");
    }

    vector<char> frames; ifstream tfmFile(tfmP); string line;
    for (int i = 0; i < 3; ++i) getline(tfmFile, line);
    while (getline(tfmFile, line)) {
        if (line.empty() || line == "#") continue;
        stringstream ss(line); int fIdx; char type;
        if (ss >> fIdx >> type && fIdx >= 0) {
            if (static_cast<size_t>(fIdx) >= frames.size()) frames.resize(static_cast<size_t>(fIdx) + 1, ' ');
            frames[fIdx] = type;
        }
    }

    vector<pair<int, string>> results;
    CycleIdentity lastID; int currentFrame = 0;

    {
        string initP = "c"; int maxC = -1;
        for (const auto& p : VALID_PATTERNS) {
            int score = CountStable(0, frames, GetIdentity(p, 0), true);
            if (score > maxC) { maxC = score; initP = p; }
        }
        if (maxC < maintainTh && IsProgressiveSection(0, frames)) initP = "c";
        results.push_back({ 0, initP });
        lastID = GetIdentity(initP, 0); currentFrame = 1;
    }

    while (static_cast<size_t>(currentFrame) + 10 <= frames.size()) {
        int oldCore = lastID.is_c ? 0 : CountStable(currentFrame, frames, lastID, false);
        CycleIdentity bestNewID;
        int bestNewCount = -1, bestNewConsecutive = -1;

        for (const auto& p : VALID_PATTERNS) {
            CycleIdentity candidate = GetIdentity(p, currentFrame);
            int c = CountStable(currentFrame, frames, candidate, true);
            int cons = CountConsecutive(currentFrame, frames, candidate);
            if (cons > bestNewConsecutive || (cons == bestNewConsecutive && c > bestNewCount)) {
                bestNewCount = c; bestNewConsecutive = cons; bestNewID = candidate;
            }
        }

        bool shouldTransition = (bestNewCount >= dominantTh) ||
            (oldCore < maintainTh && bestNewCount >= adoptionTh) ||
            (bestNewConsecutive >= consecutTh);

        if (shouldTransition && bestNewID != lastID) {
            int x = currentFrame;
            while (static_cast<size_t>(x) + 5 <= frames.size()) {
                if (MatchStrict(x, frames, bestNewID)) {
                    bool selfCollision = (!lastID.is_c && lastID.p_pos[static_cast<size_t>(x) % 5] && frames[static_cast<size_t>(x)] == 'p');
                    if (!selfCollision) break;
                }
                x++;
            }
            int xLimit = (currentFrame > 50) ? currentFrame - 50 : 0;
            while (x > 0 && x > xLimit) {
                if (MatchStrict(x - 1, frames, bestNewID)) {
                    bool prevCollision = (!lastID.is_c && lastID.p_pos[static_cast<size_t>(x - 1) % 5] && frames[static_cast<size_t>(x - 1)] == 'p');
                    if (prevCollision) break;
                    x--;
                }
                else break;
            }
            string pat = "c";
            for (const auto& p : VALID_PATTERNS) if (GetIdentity(p, x) == bestNewID) { pat = p; break; }
            results.push_back({ x, pat });
            lastID = bestNewID; currentFrame = x + 10;
        }
        else if (IsProgressiveSection(currentFrame, frames) && !lastID.is_c) {
            results.push_back({ currentFrame, "c" });
            lastID = GetIdentity("c", currentFrame);
            currentFrame += 150;
        }
        else {
            currentFrame++;
        }
    }

    ofstream outFile(outP);
    for (size_t i = 0; i < results.size(); ++i) {
        int start = results[i].first;
        int end = (i + 1 < results.size()) ? results[i + 1].first - 1 : 0;
        outFile << start << "," << end << " " << results[i].second << endl;
    }
    cout << "Successfully generated: " << outP.filename().string() << " (" << results.size() << " entries)" << endl;
    return 0;
}
