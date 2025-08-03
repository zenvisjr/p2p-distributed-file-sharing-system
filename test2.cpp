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
float K = 1.25f;

// ✅ This function is kept identical to `client.cpp`
void AssignChunksToPeers(int totalChunks, float alpha, float beta) {
  int numPeers = peerStatsMap.size();
  assignedChunks.clear();
  if (numPeers == 0)
    return;

  float maxScore = 0.0;
//   for (const auto &[_, stats] : peerStatsMap)
//     maxScore = max(maxScore, stats.score);

  unordered_map<string, float> normalizedScores;
//   for (const auto &[key, stats] : peerStatsMap)
//     normalizedScores[key] = (maxScore > 0) ? stats.score / maxScore : 0;

  int threshold = ceil(K * totalChunks / numPeers);
  unordered_map<string, int> chunksAssigned;

  for (int i = 0; i < totalChunks; ++i) {
    string bestKey = "";
    float bestUtility = -1e9;

    for (const auto &[key, peer] : peerStatsMap) {
      int assigned = chunksAssigned[key];
      if (assigned >= threshold)
        continue;

      float utility = alpha * peer.score - beta * assigned / threshold;
      if (utility > bestUtility) {
        bestUtility = utility;
        bestKey = key;
      }
    }

    if (!bestKey.empty()) {
      chunksAssigned[bestKey]++;
      const PeerStats &peer = peerStatsMap[bestKey];
      assignedChunks[i] = ChunkAssignment(i, peer);

      //   cout << "📦 Chunk " << i << " assigned to " << bestKey
      //        << " (score=" << peer.score
      //        << ", assigned=" << chunksAssigned[bestKey] << ")\n";
    } else {
      cerr << "❌ No peer available for chunk " << i << endl;
    }
  }
}

// ---------- Test Harness ----------
void runTest(int numChunks, float alpha, float beta) {
  AssignChunksToPeers(numChunks, alpha, beta);

  unordered_map<string, int> chunkCountPerPeer;
  for (const auto &[chunkIndex, assignment] : assignedChunks) {
    string peerID = assignment.assignedPeer.ip + ":" +
                    to_string(assignment.assignedPeer.port);
    chunkCountPerPeer[peerID]++;
  }

  // Summary
  int total = 0, maxChunks = 0, minChunks = 1e9;
  cout << fixed << setprecision(2);
  cout << "\n=== Results for α=" << alpha << ", β=" << beta << " ===\n";

  for (auto &[peerID, count] : chunkCountPerPeer) {
    total += count;
    maxChunks = max(maxChunks, count);
    minChunks = min(minChunks, count);
  }

  float avg = (float)total / peerStatsMap.size();
  cout << "\nSummary: Total=" << total << ", Max=" << maxChunks
       << ", Min=" << minChunks << ", Avg=" << avg << "\n";

  // Frequency mapping
  unordered_map<int, int> freq;
  for (auto &[_, count] : chunkCountPerPeer) {
    freq[count]++;
  }

  cout << "\n📊 Chunk Distribution:\n";
  for (auto &[chunks, peerCount] : freq) {
    cout << peerCount << " peers got " << chunks << " chunks\n";
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

// ---------- Peer Generation ----------
void generatePeers(int numPeers) {
  peerStatsMap.clear();
  default_random_engine gen(42);
  uniform_real_distribution<float> scoreDist(0.1, 1.0);

  for (int i = 1; i <= numPeers; i++) {
    string ip = "192.168.0." + to_string(i);
    int port = 6000 + i;
    float score = scoreDist(gen);

    PeerStats p(ip, port);
    p.score = score;
    peerStatsMap[ip + ":" + to_string(port)] = p;
  }
}

// ---------- Main ----------
int main() {
  generatePeers(50);
  int numChunks = 500;

  vector<pair<float, float>> testPairs = {{0.1, 0.9}, {0.2, 0.8}, {0.3, 0.7},
                                          {0.4, 0.6}, {0.5, 0.5}, {0.6, 0.4},
                                          {0.7, 0.3}, {0.8, 0.2}, {0.9, 0.1}};

  for (auto &[alpha, beta] : testPairs) {
    runTest(numChunks, alpha, beta);
  }

  return 0;
}
