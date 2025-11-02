#include "tetris.h"
#include <algorithm>
#include <sstream>
#include <cassert>

static constexpr uint8_t SHAPES[8][4][4] = {
/* Empty */ {{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}},
/* I */     {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
/* O */     {{0,0,0,0},{0,1,1,0},{0,1,1,0},{0,0,0,0}},
/* T */     {{0,0,0,0},{1,1,1,0},{0,1,0,0},{0,0,0,0}},
/* S */     {{0,0,0,0},{0,1,1,0},{1,1,0,0},{0,0,0,0}},
/* Z */     {{0,0,0,0},{1,1,0,0},{0,1,1,0},{0,0,0,0}},
/* J */     {{0,0,0,0},{1,1,1,0},{1,0,0,0},{0,0,0,0}},
/* L */     {{0,0,0,0},{1,1,1,0},{0,0,1,0},{0,0,0,0}},
};

static inline void rotXY(int &x, int &y, int rot) {
    int nx = x, ny = y;
    switch (rot & 3) {
        case 0: break;
        case 1: nx = 3 - y; ny = x; break;
        case 2: nx = 3 - x; ny = 3 - y; break;
        case 3: nx = y; ny = 3 - x; break;
    }
    x = nx; y = ny;
}

bool Tetris::cell(Piece p, int rot, int dx, int dy) {
    if (p == Empty) return false;
    if (dx < 0 || dx >= 4 || dy < 0 || dy >= 4) return false;
    int x = dx, y = dy;
    rotXY(x, y, rot);
    return SHAPES[p][y][x] != 0;
}

Tetris::Tetris(uint32_t seed) : rng_(seed) { reset(); }

void Tetris::reset() {
    board_.fill(0);
    bag_.clear();
    st_ = {};
    st_.level = 1;
    st_.hold = Empty;
    st_.holdLocked = false;
    newBagIfNeeded();
    spawn();
    computeGhost();
}

void Tetris::newBagIfNeeded() {
    if (!bag_.empty()) return;
    std::array<Piece,7> all{ I,O,T,S,Z,J,L };
    std::shuffle(all.begin(), all.end(), rng_);
    for (auto p : all) bag_.push_back(p);
}

Tetris::Piece Tetris::popNext() {
    newBagIfNeeded();
    Piece p = bag_.front();
    bag_.erase(bag_.begin());
    return p;
}

bool Tetris::canPlace(const Active& a) const {
    for (int py=0; py<4; ++py) for (int px=0; px<4; ++px) {
        if (!cell(a.id,a.rot,px,py)) continue;
        int bx=a.x+px, by=a.y+py;
        if (bx<0||bx>=kWidth||by>=kHeight) return false;
        if (by>=0 && board_[idx(bx,by)]!=0) return false;
    }
    return true;
}

void Tetris::spawn() {
    st_.active.id=popNext();
    st_.active.rot=0;
    st_.active.x=3;
    st_.active.y=-1;
    if (!canPlace(st_.active)) { st_.active.y=0; }
    if (!canPlace(st_.active)) st_.gameOver=true;
}

bool Tetris::testKick(Active& a,int dir) const {
    Active t=a;
    t.rot=(a.rot+(dir>0?1:3))&3;
    const std::array<std::pair<int,int>,4> kicks{{{0,0},{-1,0},{1,0},{0,-1}}};
    for(auto [kx,ky]:kicks){
        t.x=a.x+kx;t.y=a.y+ky;
        if(canPlace(t)){a=t;return true;}
    }
    return false;
}

void Tetris::lockPiece(){
    const auto&a=st_.active;
    for(int py=0;py<4;++py)for(int px=0;px<4;++px){
        if(!cell(a.id,a.rot,px,py))continue;
        int bx=a.x+px,by=a.y+py;
        if(by>=0&&by<kHeight&&bx>=0&&bx<kWidth)
            board_[idx(bx,by)]=static_cast<uint8_t>(a.id);
    }
    clearLinesAndScore();
    spawn();
}

int Tetris::clearLinesAndScore(){
    int cleared=0;
    for(int y=kHeight-1;y>=0;--y){
        bool full=true;
        for(int x=0;x<kWidth;++x) if(board_[idx(x,y)]==0){full=false;break;}
        if(full){
            ++cleared;
            for(int yy=y;yy>0;--yy)
                for(int x=0;x<kWidth;++x)
                    board_[idx(x,yy)]=board_[idx(x,yy-1)];
            for(int x=0;x<kWidth;++x) board_[idx(x,0)]=0;
            ++y;
        }
    }
    int add=0;
    switch(cleared){
        case 1:add=100;break;
        case 2:add=300;break;
        case 3:add=500;break;
        case 4:add=800;break;
    }
    st_.lines+=cleared;
    st_.level=1+st_.lines/10;
    st_.score+=add*st_.level;
    return cleared;
}

void Tetris::addLockScore(int c,int s,int h){(void)c;st_.score+=s+h*2;}

void Tetris::computeGhost(){
    Active g=st_.active;
    while(true){
        Active n=g; n.y++;
        if(!canPlace(n))break;
        g=n;
    }
    st_.ghostY=g.y;
}

bool Tetris::step(Action a){
    if(st_.gameOver)return false;
    bool changed=false;
    int sdrop=0,hdrop=0;
    auto move=[&](int dx,int dy){Active t=st_.active;t.x+=dx;t.y+=dy;if(canPlace(t)){st_.active=t;return true;}return false;};

    switch(a){
        case Action::Left:changed=move(-1,0);break;
        case Action::Right:changed=move(1,0);break;
        case Action::SoftDrop:if(move(0,1)){++sdrop;changed=true;}break;
        case Action::HardDrop:
            while(move(0,1))++hdrop;
            lockPiece();changed=true;break;
        case Action::RotateCW:{Active t=st_.active;if(testKick(t,1)){st_.active=t;changed=true;}}break;
        case Action::RotateCCW:{Active t=st_.active;if(testKick(t,-1)){st_.active=t;changed=true;}}break;
        case Action::Hold:
            if(!st_.holdLocked){
                Piece sw=st_.hold;st_.hold=st_.active.id;st_.holdLocked=true;
                if(sw==Empty)spawn();
                else{st_.active.id=sw;st_.active.rot=0;st_.active.x=3;st_.active.y=-1;if(!canPlace(st_.active))st_.gameOver=true;}
                changed=true;
            }
            break;
        default:break;
    }

    if(a!=Action::HardDrop){
        Active t=st_.active;t.y++;
        if(canPlace(t)){st_.active=t;changed=true;}
        else{lockPiece();changed=true;}
    }

    addLockScore(0,sdrop,hdrop);
    computeGhost();
    return changed;
}

std::string Tetris::debugString() const {
    std::array<char,kWidth*kHeight> bg{};
    for(int y=0;y<kHeight;++y)
        for(int x=0;x<kWidth;++x)
            bg[idx(x,y)]=(board_[idx(x,y)]?'X':' ');
    for(int py=0;py<4;++py)for(int px=0;px<4;++px)
        if(cell(st_.active.id,st_.active.rot,px,py)){
            int gx=st_.active.x+px,gy=st_.ghostY+py;
            if(gy>=0&&gy<kHeight&&gx>=0&&gx<kWidth&&bg[idx(gx,gy)]==' ')bg[idx(gx,gy)]='.';
        }
    for(int py=0;py<4;++py)for(int px=0;px<4;++px)
        if(cell(st_.active.id,st_.active.rot,px,py)){
            int ax=st_.active.x+px,ay=st_.active.y+py;
            if(ay>=0&&ay<kHeight&&ax>=0&&ax<kWidth)bg[idx(ax,ay)]='#';
        }

    std::ostringstream oss;
    oss<<"+----------+\n";
    for(int y=0;y<kHeight;++y){
        oss<<'|';
        for(int x=0;x<kWidth;++x)oss<<bg[idx(x,y)];
        oss<<"|\n";
    }
    oss<<"+----------+\n";
    oss<<"Score:"<<st_.score<<" Lines:"<<st_.lines<<" Level:"<<st_.level<<(st_.gameOver?" [GAME OVER]\n":"\n");
    return oss.str();
}

// --- JSON serialization ---
json Tetris::to_json() const {
    json j;
    std::vector<std::vector<int>> board2D(kHeight, std::vector<int>(kWidth));
    for (int y=0; y<kHeight; ++y)
        for (int x=0; x<kWidth; ++x)
            board2D[y][x] = board_[idx(x,y)];

    const auto& s = st_;
    j = {
        {"board", board2D},
        {"active", {{"id", s.active.id}, {"x", s.active.x}, {"y", s.active.y}, {"rot", s.active.rot}}},
        {"ghostY", s.ghostY},
        {"hold", s.hold},
        {"holdLocked", s.holdLocked},
        {"next", s.nextPreview},
        {"score", s.score},
        {"lines", s.lines},
        {"level", s.level},
        {"combo", s.combo},
        {"gameOver", s.gameOver}
    };
    return j;
}

void Tetris::from_json(const json& j) {
    auto b2D = j.at("board").get<std::vector<std::vector<int>>>();
    for (int y=0; y<kHeight; ++y)
        for (int x=0; x<kWidth; ++x)
            board_[idx(x,y)] = static_cast<uint8_t>(b2D[y][x]);
    auto a=j.at("active");
    st_.active.id=a["id"];
    st_.active.x=a["x"];
    st_.active.y=a["y"];
    st_.active.rot=a["rot"];
    st_.ghostY=j["ghostY"];
    st_.hold=j["hold"];
    st_.holdLocked=j["holdLocked"];
    st_.nextPreview=j["next"].get<std::array<Piece,6>>();
    st_.score=j["score"];
    st_.lines=j["lines"];
    st_.level=j["level"];
    st_.combo=j["combo"];
    st_.gameOver=j["gameOver"];
}
