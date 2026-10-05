# Ports of Plague 3D

*The 3D version of [Ports of Plague](https://github.com/cksarge/Ports-of-Plague), an educational board game about the Black Death, 1347–1353, for 1–6 players, on one screen or each on their own device. Made in Unreal Engine 5.8.*

It is the same game as the web version: the same rules, numbers, cards and historical facts, read from the same data files. What is new is the board. The map of Europe is a 3D model with hills, mountains, forests and moving water. Walled towns change as the plague arrives and passes, flags mark each house's trading posts, ships and carts travel the routes, the dice are real cubes, and the game ends with a finale staged on the map.

Play the web version at https://portsofplague.carterscoding.com/. Its repository has the rule book, the research sheet and the sources.

## Play

Download the disk image (**.dmg**) from the [Releases](https://github.com/cksarge/Ports-of-Plague-3D/releases) page, open it, and drag **Ports of Plague** onto the Applications folder beside it. It runs on a Mac with Apple silicon (macOS 14 or later). The first time, macOS may ask you to confirm that you want to open it.

There is no Windows build yet.

- Choose 2–6 houses, their names and home cities, the game length and the difficulty. Any house can be played by a bot.
- With more than one person, the screen says when to pass the computer on.
- The game saves itself after every move. **Continue saved game** on the menu picks it up again.

### Everyone on their own device

The 3D game can be the big screen of a game played from phones, tablets or laptops. Nothing has to be installed on them: they use the web version's page.

1. On the New Game screen choose **Everyone on their own device**. The screen shows a four-character room code.
2. Each player opens https://portsofplague.carterscoding.com/ on their own device, chooses **Join a game**, types the code, and names their house.
3. Fill any empty seats with bots if you like, then press **Roll for turn order**.

Everyone watches the map on the big screen and takes their turn on their own device. Any device can press Next on a card. A device that reloads or drops out goes back to its house by joining the same room again, and a saved game reopens its room when it is continued. This needs an internet connection: messages between the screens pass through the same relay service the web version uses, and nothing is stored there.

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
| `node Tools/nettest/devices.mjs --code=XXXX` | joins a running game's room as several players' devices and plays it, to check multi-device play (see the top of the file) |
| `Tools/package_mac.sh` | makes `Saved/Package/Mac/PortsOfPlague.app` |
| `Tools/make_dmg.sh` | makes the disk image people download, `Saved/Package/Ports-of-Plague.dmg`, from that app |
| `Tools/sync_data.sh` | copies the web version's data files into `Content/Data` (`--check` only compares) |
| `Tools/build_content.sh` | makes the materials and imports the textures, music and sounds |

Or open `PortsOfPlague.uproject` in the editor and press Play.

The engine is expected at `/Users/Shared/Epic Games/UE_5.8`. Set `UE_ROOT` if yours is elsewhere.

### How it is put together

- **`Source/PortsEngine`** is the rules: a C++ port of the web version's engine with no graphics in it. Its tests replay 240 games recorded from the web engine and compare the whole game state after every move, so the two versions stay the same game.
- **`Source/PortsOfPlague`** is everything you see and hear: the map, the camera, the screens, the finale and the sound. It also holds the big screen's side of multi-device play (`PortsNet`), a port of the web version's room and message code that speaks to the same relay service, so the web version's join page works with it unchanged.
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
- **Multi-device play:** messages are relayed by Supabase Realtime.
- **Certificates:** the list of certificate authorities in `Content/Certificates` is Mozilla's, as extracted by the curl project, under the Mozilla Public License 2.0.

Ports of Plague uses Unreal® Engine. Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere.

Unreal® Engine, Copyright 1998 – 2026, Epic Games, Inc. All rights reserved.

## Trademarks

The game opens with the Unreal Engine splash screen. See [TRADEMARKS.txt](TRADEMARKS.txt).

## License

MIT. See [LICENSE](LICENSE).
