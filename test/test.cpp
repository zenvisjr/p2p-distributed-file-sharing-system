#include <bits/stdc++.h>
using namespace std;

// ---------- Structures ----------
struct PeerStats {
  string ip;
  int port;
  float score = 1.0;
  int chunksServed = 0;
  double totalDownloadTime = 0.0;

  PeerStats() : ip(""), port(0) {}
  PeerStats(string i, int p) : ip(i), port(p) {}
};

struct ChunkAssignment {
  int chunkIndex;
  PeerStats assignedPeer;

  ChunkAssignment() : chunkIndex(-1), assignedPeer(PeerStats("", 0)) {}
  ChunkAssignment(int i, PeerStats p) : chunkIndex(i), assignedPeer(p) {}
};

// ---------- Globals ----------
unordered_map<string, PeerStats> peerStatsMap;
unordered_map<int, ChunkAssignment> assignedChunks;

float kThreshold = 1.25f;

// ---------- Test Harness ----------
void runTest(int numChunks, float alpha, float beta) {
  assignedChunks.clear();
  unordered_map<string, int> chunkCountPerPeer;

  int numPeers = peerStatsMap.size();
  int maxAllowedChunks = ceil(((float)numChunks / numPeers) * kThreshold);
  cout << "Max allowed chunks per peer: " << maxAllowedChunks << endl;

  for (int i = 0; i < numChunks; i++) {
    float bestScore = -1e9;
    string bestPeerID = "";

    for (const auto &[peerID, stats] : peerStatsMap) {
      int currentLoad = chunkCountPerPeer[peerID];

      if (currentLoad >= maxAllowedChunks)
        continue;

      float normalizedScore = stats.score; // Already relative
      float penalty = (float)currentLoad / maxAllowedChunks;

      float finalScore = alpha * normalizedScore - beta * penalty;
      // float finalScore = stats.score;

      if (finalScore > bestScore) {
        bestScore = finalScore;
        bestPeerID = peerID;
      }
    }

    if (!bestPeerID.empty()) {
      chunkCountPerPeer[bestPeerID]++;
      assignedChunks[i] = ChunkAssignment(i, peerStatsMap[bestPeerID]);
    } else {
      cerr << "⚠️ No eligible peer found for chunk " << i << endl;
    }
  }

  // ---------- Summary ----------
  int total = 0, maxChunks = 0, minChunks = 1e9;
  cout << fixed << setprecision(2);
  cout << "\n=== Results for α=" << alpha << ", β=" << beta << " ===\n";

  for (auto &[peerID, count] : chunkCountPerPeer) {
    // cout << peerID << " -> " << count << " chunks\n";
    total += count;
    maxChunks = max(maxChunks, count);
    minChunks = min(minChunks, count);
  }

  float avg = (float)total / peerStatsMap.size();
  cout << "\nSummary: Total=" << total << ", Max=" << maxChunks
       << ", Min=" << minChunks << ", Avg=" << avg << "\n";

  // ---------- Frequency Map ----------
  unordered_map<int, int> freq;
  for (auto &[peerID, count] : chunkCountPerPeer) {
    freq[count]++;
  }

  cout << "\n📊 Chunk Distribution:\n";
  for (auto &[chunks, peerCount] : freq) {
    cout << peerCount << " peers got " << chunks << " chunks\n";
  }

  // 🔽 New: Score-based descending summary
  vector<pair<float, int>> scoreToChunks;
  for (const auto &[peerID, count] : chunkCountPerPeer) {
    float score = peerStatsMap[peerID].score;
    scoreToChunks.emplace_back(score, count);
  }

  sort(scoreToChunks.rbegin(), scoreToChunks.rend());

  unordered_map<int, int> scoreGroup;
  for (auto &[score, chunks] : scoreToChunks) {
    int roundedScore = round(score * 100); // 0.4378 -> 44
    scoreGroup[roundedScore]++;
  }

  // ✅ Step 1: Copy to sorted map in descending order
  map<int, int, greater<int>> sortedScoreGroup;
  for (auto &[roundedScore, peerCount] : scoreGroup) {
    sortedScoreGroup[roundedScore] = peerCount;
  }

  // ✅ Step 2: Print in descending order
  cout << "\n📈 Score-wise Peer Distribution:\n";
  for (auto &[roundedScore, peerCount] : sortedScoreGroup) {
    cout << fixed << setprecision(2) << (float)(roundedScore) / 100 << " - "
         << peerCount << " peers\n";
  }

  // ✅ Score → Total Chunks Assigned
  unordered_map<int, int>
      scoreToChunkCount; // rounded_score_int -> total_chunks

  for (auto &[chunkIndex, assignment] : assignedChunks) {
    float score = assignment.assignedPeer.score;
    int rounded = round(score * 100); // e.g., 0.648 → 65
    scoreToChunkCount[rounded]++;
  }

  // ✅ Sort by score descending
  map<int, int, greater<int>> sortedScoreToChunks(scoreToChunkCount.begin(),
                                                  scoreToChunkCount.end());

  cout << "\n📈 Score → Chunk Count:\n";
  for (auto &[roundedScore, chunkCount] : sortedScoreToChunks) {
    cout << fixed << setprecision(2) << (float)(roundedScore) / 100
         << " score peer → " << chunkCount << " chunks\n";
  }
}

// ---------- Generate Sample Peer Stats ----------
void generatePeers(int numPeers) {
  peerStatsMap.clear();
  default_random_engine gen;
  uniform_real_distribution<float> scoreDist(0.1, 1.0); // relative scores

  for (int i = 1; i <= numPeers; i++) {
    string ip = "192.168.0." + to_string(i);
    int port = 6000 + i;
    float score = scoreDist(gen);
    // float score = 1;

    PeerStats p(ip, port);
    p.score = score;
    peerStatsMap[ip + ":" + to_string(port)] = p;
  }
}

// ---------- Main ----------
int main() {
  generatePeers(4); // 50 peers
  int numChunks = 672;

  vector<pair<float, float>> testPairs = {{0.1, 0.9}, {0.2, 0.8}, {0.3, 0.7},
                                          {0.4, 0.6}, {0.5, 0.5}, {0.6, 0.4},
                                          {0.7, 0.3}, {0.8, 0.2}, {0.9, 0.1}};

  // for (auto &[alpha, beta] : testPairs) {
    runTest(numChunks, .2, .8);
  // }

  return 0;
}
