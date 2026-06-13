# Publishing & Playing — Let Friends Try It and Compete Against Bots

The engine now ships a **UCI** front-end (`build_uci/chess_uci`), the universal
protocol every chess GUI and online bot bridge understands. That unlocks three
things: friends playing it locally, friends playing it online, and the engine
earning a real public rating against other bots.

```bash
bash tools/build_uci.sh        # -> build_uci/chess_uci  (Linux or Windows/MinGW)
```
On Windows the same sources build `chess_uci.exe` with MinGW g++. Keep
`resources/Book.txt` next to the binary to enable the opening book (optional).

Quick self-test (type these after launching `./build_uci/chess_uci`):
```
uci
position startpos moves e2e4 e7e5
go movetime 1000
```
You should get `uciok`, then a `bestmove`.

---

## 1. Friends try it locally (any OS, no coding)

Give them the `chess_uci` binary (+ `resources/Book.txt`) and a free UCI GUI.
They just "Add Engine" and point it at the binary:

| GUI | OS | Notes |
|-----|----|-------|
| **En Croissant** | Win/Mac/Linux | Modern, easy; great for playing an engine |
| **Cute Chess** | Win/Mac/Linux | Also runs engine-vs-engine matches/tournaments |
| **Arena** | Windows | Classic, free |
| **BanksiaGUI** | Win/Mac/Linux | Polished, tournaments |
| **Nibbler** | Win/Mac/Linux | Analysis-style board |

Or hand Windows friends the existing GUI build (`build_gui_new.bat` →
`chess_gui.exe`) — that's the click-to-play app, no UCI needed, but Windows-only.

The cleanest way to distribute: cut a **GitHub Release** and attach the
binaries + `Book.txt` (and the `resources/pieces/` folder if you ship the GUI).

---

## 2. Put it on Lichess as a BOT — play online & gain a rating

This is how it competes against other bots and earns a public Elo. Lichess
provides an official bridge that talks UCI to your engine.

1. **Create a fresh Lichess account** (bot accounts must have *no prior games*).
2. Make a **personal API token** at <https://lichess.org/account/oauth/token>
   with the **`bot:play`** scope.
3. **Upgrade the account to a bot** (one-time, irreversible — use a dedicated
   account):
   ```bash
   curl -d '' https://lichess.org/api/bot/account/upgrade \
        -H "Authorization: Bearer <YOUR_TOKEN>"
   ```
4. **Install the bridge** (<https://github.com/lichess-bot-devs/lichess-bot>):
   ```bash
   git clone https://github.com/lichess-bot-devs/lichess-bot
   cd lichess-bot
   pip install -r requirements.txt
   cp config.yml.default config.yml
   ```
5. **Point it at this engine.** Copy `chess_uci` (and `Book.txt`) into
   `lichess-bot/engines/` and edit `config.yml`:
   ```yaml
   token: "<YOUR_TOKEN>"
   engine:
     dir: "./engines/"
     name: "chess_uci"          # or chess_uci.exe on Windows
     protocol: "uci"
     uci_options:
       Threads: 4
       Hash: 256
       OwnBook: true
   ```
6. **Run it:** `python lichess-bot.py`
7. The bot now accepts challenges. Turn on automatic matchmaking against other
   bots in `config.yml` (`matchmaking: allow_matchmaking: true`) so it plays
   continuously and builds a rating. Friends play it by challenging its profile
   URL: `https://lichess.org/@/<botname>`.

Notes:
- Use short/blitz time controls first — on modest hardware the engine reaches
  ~12–14 ply (see `RESULTS.md`), so longer controls = stronger play.
- Lichess BOT ratings are separate from human ratings but are real and public.

---

## 3. Compete in engine tournaments offline

Use **Cute Chess** (`cutechess-cli`) or **BanksiaGUI** to run gauntlets against
other UCI engines (e.g. download Stash, Fairy-Stockfish, or weaker bots to
calibrate). Example with cutechess-cli:
```bash
cutechess-cli \
  -engine cmd=./build_uci/chess_uci name=AIPowers \
  -engine cmd=./other_engine \
  -each tc=10+0.1 proto=uci -games 100 -pgnout results.pgn
```
This gives a relative Elo vs a known opponent — the most rigorous way to track
real strength as you keep improving the engine.

---

## Current limitations to be aware of
- **Auto-queen only** — the engine never underpromotes (rare but can matter).
- **No `ponder`**, and `stop` is best-effort (search is synchronous per `go`).
- Strength is throughput-bound on this hardware (~0.5M nps single-thread). See
  `TODO.md` for the speed roadmap (lazy eval, bitboards) that would raise its
  online rating.
