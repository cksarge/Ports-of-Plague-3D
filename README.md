# Ports of Plague 3D

*The 3D version of [Ports of Plague](https://github.com/cksarge/Ports-of-Plague), an educational board game about the Black Death, 1347–1353, for 1–6 players on one screen. Made in Unreal Engine 5.8.*

It is the same game as the web version: the same rules, numbers, cards and historical facts, read from the same data files. What is new is the board. The map of Europe is a 3D model with hills, mountains, forests and moving water. Walled towns change as the plague arrives and passes, flags mark each house's trading posts, ships and carts travel the routes, the dice are real cubes, and the game ends with a finale staged on the map.

Play the web version at https://portsofplague.carterscoding.com/. Its repository has the rule book, the research sheet and the sources.

## Play

Download **PortsOfPlague.app** from the [Releases](https://github.com/cksarge/Ports-of-Plague-3D/releases) page and open it. It runs on a Mac with Apple silicon (macOS 14 or later) and needs no installation. The first time, macOS may ask you to confirm that you want to open it.

There is no Windows build yet.

- Choose 2–6 houses, their names and home cities, the game length and the difficulty. Any house can be played by a bot.
- With more than one person, the screen says when to pass the computer on.
- The game saves itself after every move. **Continue saved game** on the menu picks it up again.

### Controls

| | |
|---|---|
| Move the map | drag, or the arrow keys or WASD |
| Zoom | mouse wheel, two fingers on a trackpad, or **+** and **−** |
| Tilt the camera | hold the right button (two fingers on a trackpad) and move up or down, or Page Up and Page Down |
| Whole map again | **H** |
| Actions | **1–6**, **7** marriage, **8** land, **9** loan, **0** partnership, **G** close gates |
| End turn | **E** |
| Rules, Historian's Journal | **R**, **J** |
| Sound effects, music | **M**, **N** |
| Confirm, cancel | **Enter**, **Esc** |

## Build it yourself

You need a Mac with Unreal Engine 5.8, Xcode and Node.js. Google Chrome is needed only to make the pictures and sounds again.

```sh
git clone --recurse-submodules https://github.com/cksarge/Ports-of-Plague-3D
```

The web version comes along as a submodule in `ports_web_reference_READ_ONLY`. Nothing here changes it: the 3D game copies its data files and draws its pictures, fonts, music and sound effects from it.

Close the Unreal editor before running any of these.

| Command | What it does |
|---|---|
| `Tools/build.sh` | compiles the game for the editor |
| `Tools/run.sh` | runs the game in a window, without the editor |
| `Tools/test.sh` | runs the rules tests |
| `Tools/package_mac.sh` | makes `Saved/Package/Mac/PortsOfPlague.app` |
| `Tools/sync_data.sh` | copies the web version's data files into `Content/Data` (`--check` only compares) |
| `Tools/build_content.sh` | makes the materials and imports the textures, music and sounds |

Or open `PortsOfPlague.uproject` in the editor and press Play.

The engine is expected at `/Users/Shared/Epic Games/UE_5.8`. Set `UE_ROOT` if yours is elsewhere.

### How it is put together

- **`Source/PortsEngine`** is the rules: a C++ port of the web version's engine with no graphics in it. Its tests replay 240 games recorded from the web engine and compare the whole game state after every move, so the two versions stay the same game.
- **`Source/PortsOfPlague`** is everything you see and hear: the map, the camera, the screens, the finale and the sound.
- **`Content/Data`** holds unchanged copies of the web version's data files. The rules, the card text and every historical fact come from there and nowhere else.
- **`Tools`** holds the scripts above, and the ones that draw the game's pictures and record its sound effects from the web version's own code.

Nearly everything is made by code rather than in the editor: the level is empty, and the map, towns, ships and screens are built when the game starts.

## Credits

Ports of Plague was made by Carter K, Landon S, Valen H and John-Paul T for a high-school history class.

- **History:** the facts and sources are the web version's; see its Historian's Journal and research sheet.
- **Map:** coastlines, rivers and lakes from Natural Earth (public domain).
- **Fonts:** EB Garamond, Cinzel and UnifrakturMaguntia, under the SIL Open Font License. The licences are in `Content/UI/Fonts`.
- **Music:** by Kevin MacLeod (incompetech.com), licensed under Creative Commons: By Attribution 4.0. The game's About page lists each song.
- **Sound effects:** original, made by the game's own code.
- **Dice:** adapted from roll-a-die (© 2015 ukatama), under the MIT License.

## License

MIT. See [LICENSE](LICENSE).
