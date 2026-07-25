#include "world.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "world_internal.h"

namespace game {

bool GenerateInteriorGraphGallery(
    const std::filesystem::path& outputDirectory, int count) {
    if (count <= 0) return false;
    std::error_code error;
    std::filesystem::create_directories(outputDirectory, error);
    if (error) return false;
    static constexpr const char* archetypes[]{
        "circle", "triangle", "charger", "shooter"};
    std::ofstream index(outputDirectory / "index.html", std::ios::trunc);
    if (!index) return false;
    index << "<!doctype html><meta charset=\"utf-8\"><title>Enemy interior "
             "graphs</title><style>body{background:#090d15;color:#fff;"
             "font:16px sans-serif}main{display:grid;grid-template-columns:"
             "repeat(auto-fit,minmax(430px,1fr));gap:18px}figure{margin:0;"
             "padding:12px;background:#121a26;border-radius:8px}img{width:100%;"
             "background:#070b11}figcaption{padding:8px 0}</style><h1>Enemy "
             "interior graph samples</h1><p>Green = spawn; colored nodes = "
             "organs; thick cyan path = graph diameter.</p><main>";
    const std::string savedArchetype = interior.archetype;
    const std::string savedEnemy = interior.enemy;
    for (int sample = 0; sample < count; ++sample) {
        const char* archetype = archetypes[sample % std::size(archetypes)];
        const std::uint64_t seed = ConnectionSeed(
            0x494e544552494f52ULL, sample, sample % 4);
        interior.archetype = archetype;
        interior.enemy = archetype;
        GenerateRooms(seed);
        int maxRow = 0, maxColumn = 0, spawn = 0;
        for (int room = 0; room < static_cast<int>(rooms.size()); ++room) {
            maxRow = std::max(maxRow, rooms[room].row);
            maxColumn = std::max(maxColumn, rooms[room].column);
            if (rooms[room].distance == 0) spawn = room;
        }
        std::vector<int> degree(rooms.size(), 0);
        std::vector<std::vector<int>> neighbors(rooms.size());
        for (const auto& edge : roomConnections) {
            ++degree[edge.first];
            ++degree[edge.second];
            neighbors[edge.first].push_back(edge.second);
            neighbors[edge.second].push_back(edge.first);
        }
        const auto farthest = [&](int source, std::vector<int>* parent) {
            std::vector<int> distance(rooms.size(), -1);
            if (parent) parent->assign(rooms.size(), -1);
            std::vector<int> queue{source};
            distance[source] = 0;
            int result = source;
            for (std::size_t cursor = 0; cursor < queue.size(); ++cursor) {
                const int current = queue[cursor];
                if (distance[current] > distance[result]) result = current;
                for (int next : neighbors[current])
                    if (distance[next] < 0) {
                        distance[next] = distance[current] + 1;
                        if (parent) (*parent)[next] = current;
                        queue.push_back(next);
                    }
            }
            return result;
        };
        const int pathStart = farthest(spawn, nullptr);
        std::vector<int> parent;
        const int pathEnd = farthest(pathStart, &parent);
        std::set<std::pair<int, int>> backbone;
        for (int room = pathEnd; room != pathStart && room >= 0;
             room = parent[room])
            backbone.insert(RoomEdge(room, parent[room]));

        std::ostringstream filename;
        filename << std::setw(2) << std::setfill('0') << sample + 1 << "-"
                 << archetype << ".svg";
        std::ofstream svg(outputDirectory / filename.str(), std::ios::trunc);
        if (!svg) return false;
        const int width = 120 + maxColumn * 80;
        const int height = 150 + maxRow * 80;
        svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
            << "\" height=\"" << height << "\" viewBox=\"0 0 " << width
            << " " << height << "\"><rect width=\"100%\" height=\"100%\" "
               "fill=\"#070b11\"/><text x=\"20\" y=\"28\" fill=\"white\" "
               "font-family=\"sans-serif\" font-size=\"18\">"
            << archetype << " · seed " << seed << "</text>";
        for (const auto& edge : roomConnections) {
            const Room& first = rooms[edge.first];
            const Room& second = rooms[edge.second];
            const bool mainPath = backbone.count(edge) != 0;
            svg << "<line x1=\"" << 60 + first.column * 80 << "\" y1=\""
                << 70 + first.row * 80 << "\" x2=\""
                << 60 + second.column * 80 << "\" y2=\""
                << 70 + second.row * 80 << "\" stroke=\""
                << (mainPath ? "#49d8e8" : "#64748b")
                << "\" stroke-width=\"" << (mainPath ? 7 : 4) << "\"/>";
        }
        static constexpr const char* organColors[]{
            "#ff5c8a", "#f5b942", "#a879ff", "#55d98b"};
        for (int room = 0; room < static_cast<int>(rooms.size()); ++room) {
            int organ = -1;
            for (int indexValue = 0;
                 indexValue < static_cast<int>(organs.size()); ++indexValue)
                if (organs[indexValue].room == room) organ = indexValue;
            const char* color = room == spawn ? "#32e875" :
                organ >= 0 ? organColors[organ % 4] : "#d8e0ea";
            const int x = 60 + rooms[room].column * 80;
            const int y = 70 + rooms[room].row * 80;
            svg << "<circle cx=\"" << x << "\" cy=\"" << y
                << "\" r=\"18\" fill=\"" << color
                << "\" stroke=\"#071018\" stroke-width=\"3\"/>";
            if (room == spawn || organ >= 0)
                svg << "<text x=\"" << x << "\" y=\"" << y + 5
                    << "\" text-anchor=\"middle\" fill=\"#071018\" "
                       "font-family=\"sans-serif\" font-weight=\"bold\">"
                    << (room == spawn ? "S" :
                        organs[organ].id.substr(0, 1)) << "</text>";
        }
        svg << "</svg>";
        index << "<figure><img src=\"" << filename.str()
              << "\"><figcaption>" << sample + 1 << ". " << archetype
              << " · " << rooms.size() << " rooms · "
              << roomConnections.size() << " connections</figcaption></figure>";
    }
    interior.archetype = savedArchetype;
    interior.enemy = savedEnemy;
    index << "</main>";
    return true;
}

bool GenerateInteriorGraphTuner(const std::filesystem::path& outputFile) {
    std::error_code error;
    std::filesystem::create_directories(outputFile.parent_path(), error);
    if (error) return false;
    std::ofstream output(outputFile, std::ios::trunc);
    if (!output) return false;
    static constexpr const char* archetypes[]{
        "circle", "triangle", "charger", "shooter"};
    output << R"HTML(<!doctype html><html><head><meta charset="utf-8">
<title>Enemy Interior Bloom Tuner</title><style>
:root{color-scheme:dark;font-family:Segoe UI,sans-serif}body{margin:0;background:#080c13;color:#e8eef7}
header{position:sticky;top:0;z-index:2;background:#101722ee;padding:16px 22px;border-bottom:1px solid #293548}
h1{margin:0 0 12px;font-size:22px}.controls{display:flex;flex-wrap:wrap;gap:12px 22px;align-items:end}
label{display:grid;grid-template-columns:145px 170px 70px;gap:8px;align-items:center;font-size:13px}
input[type=range]{width:170px}input[type=number],input[type=text]{background:#070b11;color:#fff;border:1px solid #40516b;border-radius:4px;padding:6px}
button{background:#3ed0a1;color:#06120e;border:0;border-radius:5px;padding:9px 16px;font-weight:700;cursor:pointer}
main{display:grid;grid-template-columns:repeat(2,minmax(360px,1fr));gap:18px;padding:18px}
section{background:#111925;border:1px solid #263448;border-radius:8px;padding:12px}h2{margin:0 0 8px;font-size:17px}
svg{width:100%;height:440px;background:#070b11;border-radius:5px}.legend{color:#a9b7ca;font-size:12px;margin:8px 0 0}
@media(max-width:850px){main{grid-template-columns:1fr}label{grid-template-columns:120px 1fr 64px}}
</style></head><body><header><h1>Enemy Interior Bloom Tuner</h1><div class="controls">
<label>Path chance %<input id="pathChance" type="range" min="5" max="100" value="78"><input data-for="pathChance" type="number" min="5" max="100" value="78"></label>
<label>Branch chance %<input id="branchChance" type="range" min="0" max="100" value="24"><input data-for="branchChance" type="number" min="0" max="100" value="24"></label>
<label>Fork organ chance %<input id="branchOrganChance" type="range" min="0" max="100" value="55"><input data-for="branchOrganChance" type="number" min="0" max="100" value="55"></label>
<label>Max solo length<input id="maxLength" type="range" min="1" max="8" value="2"><input data-for="maxLength" type="number" min="1" max="8" value="2"></label>
<label>Organ count<input id="organCount" type="range" min="1" max="8" value="4"><input data-for="organCount" type="number" min="1" max="8" value="4"></label>
<label>Generation attempts<input id="attempts" type="range" min="1" max="500" value="256"><input data-for="attempts" type="number" min="1" max="500" value="256"></label>
<label>Seed<input id="seed" type="text" value="1234567"><span></span></label>
<button id="regenerate">Regenerate</button></div></header><main id="maps"></main>
<script>const shapes={)HTML";
    for (std::size_t typeIndex = 0;
         typeIndex < std::size(archetypes); ++typeIndex) {
        if (typeIndex) output << ",";
        output << archetypes[typeIndex] << ":[";
        const auto found = types.find(archetypes[typeIndex]);
        bool first = true;
        if (found != types.end())
            for (std::size_t row = 0; row < found->second.sprite.size(); ++row)
                for (std::size_t column = 0;
                     column < found->second.sprite[row].size(); ++column)
                    if (found->second.sprite[row][column].occupied) {
                        if (!first) output << ",";
                        first = false;
                        output << "[" << row << "," << column << "]";
                    }
        output << "]";
    }
    output << R"HTML(};
const colors=["#ff5c8a","#f5b942","#a879ff","#55d98b","#4fb7ff","#ff8b4f","#d971ff","#8de35b"];
function hash(a,b,c){let x=BigInt.asUintN(64,BigInt(a)^BigInt(b+1)*0x9e3779b97f4a7c15n^BigInt(c+1)*0xbf58476d1ce4e5b9n);x^=x>>30n;x=BigInt.asUintN(64,x*0xbf58476d1ce4e5b9n);x^=x>>27n;x=BigInt.asUintN(64,x*0x94d049bb133111ebn);return BigInt.asUintN(64,x^(x>>31n))}
function bloom(cells,seed,cfg){const at=new Map(cells.map((p,i)=>[p.join(","),i])),dirs=[[-1,0],[1,0],[0,-1],[0,1]];
 for(let attempt=0;attempt<cfg.attempts;attempt++){const s=hash(seed,attempt,cells.length);let spawn=Number(hash(s,0,cells.length)%BigInt(cells.length));
  const neighbors=r=>dirs.map(d=>at.get((cells[r][0]+d[0])+","+(cells[r][1]+d[1]))).filter(n=>n!==undefined);
  for(let o=0;o<cells.length;o++){const c=(spawn+o)%cells.length;if(neighbors(c).length){spawn=c;break}}
  if(!neighbors(spawn).length)continue;
  const visited=new Set([spawn]),edges=new Map(),organs=[],pending=[{room:spawn,length:0,rollback:[]}];let decision=0;
  while(pending.length&&organs.length<cfg.organCount){const path=pending.pop();
   if(path.length>=cfg.maxLength&&path.room!==spawn){organs.push(path.room);continue}
   let candidates=neighbors(path.room).filter(n=>!visited.has(n));
   candidates.sort((a,b)=>hash(s,path.room,a+decision)<hash(s,path.room,b+decision)?-1:1);let opened=[];
   if(path.room===spawn){if(candidates.length)opened=[candidates[0]]}
   else{for(const next of candidates){const roll=hash(s,path.room+decision++,next);if(Number(roll%100n)>=cfg.pathChance)continue;if(opened.length&&Number((roll>>16n)%100n)>=cfg.branchChance)continue;opened.push(next)}}
   if(!opened.length){if(path.room!==spawn)organs.push(path.room);continue}
   const branch=opened.length>1;
   if(branch&&Number(hash(s,path.room,decision++)%100n)<cfg.branchOrganChance)organs.push(path.room);
   for(const next of opened){visited.add(next);const key=[path.room,next].sort((a,b)=>a-b).join("-");edges.set(key,[path.room,next]);
    pending.push({room:next,length:branch?1:path.length+1,rollback:(branch?[]:path.rollback.slice()).concat(key)})}
   if(organs.length>=cfg.organCount)break}
  if(organs.length<cfg.organCount)continue;for(const path of pending)for(const key of path.rollback)edges.delete(key);
  const connected=new Set([spawn]),queue=[spawn];for(let q=0;q<queue.length;q++)for(const e of edges.values()){const n=e[0]===queue[q]?e[1]:e[1]===queue[q]?e[0]:-1;if(n>=0&&!connected.has(n)){connected.add(n);queue.push(n)}}
  return{spawn,organs,edges:[...edges.values()],connected,attempt}}
 return{spawn:0,organs:[],edges:[],connected:new Set([0]),attempt:-1}}
function render(name,cells,seed,cfg){const graph=bloom(cells,seed,cfg),rows=Math.max(...cells.map(p=>p[0])),cols=Math.max(...cells.map(p=>p[1])),w=100+cols*72,h=100+rows*58;
 const lines=graph.edges.map(e=>`<line x1="${50+cells[e[0]][1]*72}" y1="${50+cells[e[0]][0]*58}" x2="${50+cells[e[1]][1]*72}" y2="${50+cells[e[1]][0]*58}" stroke="#7f91aa" stroke-width="5"/>`).join("");
 const nodes=cells.map((p,i)=>{const organ=graph.organs.indexOf(i),active=graph.connected.has(i),fill=i===graph.spawn?"#32e875":organ>=0?colors[organ%colors.length]:active?"#e3e9f1":"#26313f",text=i===graph.spawn?"S":organ>=0?String(organ+1):"";
 return`<circle cx="${50+p[1]*72}" cy="${50+p[0]*58}" r="17" fill="${fill}" stroke="${active?"#091019":"#18202b"}" stroke-width="3" opacity="${active?1:.42}"/><text x="${50+p[1]*72}" y="${55+p[0]*58}" text-anchor="middle" fill="#071018" font-weight="bold">${text}</text>`}).join("");
 return`<section><h2>${name.toUpperCase()} · attempt ${graph.attempt+1} · ${graph.connected.size}/${cells.length} rooms used</h2><svg viewBox="0 0 ${w} ${h}">${lines}${nodes}</svg><p class="legend">Green S = single-exit spawn · numbered colors = organs on forks or dead ends · dim nodes = sealed, unreachable rooms</p></section>`}
function values(){return{pathChance:+pathChance.value,branchChance:+branchChance.value,branchOrganChance:+branchOrganChance.value,maxLength:+maxLength.value,organCount:+organCount.value,attempts:+attempts.value}}
function regenerate(){let base;try{base=BigInt(seed.value||0)}catch{base=1n}maps.innerHTML=Object.entries(shapes).map(([name,cells],i)=>render(name,cells,hash(base,i,0),values())).join("")}
document.querySelectorAll("input[type=range]").forEach(r=>{const n=document.querySelector(`[data-for="${r.id}"]`);r.oninput=()=>{n.value=r.value;regenerate()};n.oninput=()=>{r.value=n.value;regenerate()}})
seed.onchange=regenerate;regenerate.onclick=regenerate;document.getElementById("regenerate").onclick=()=>{seed.value=(BigInt(Date.now())*1000003n).toString();regenerate()};regenerate();
</script></body></html>)HTML";
    return true;
}

}  // namespace game
