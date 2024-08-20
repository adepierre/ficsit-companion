<h1 align="center" style="margin-top: 0px;">Ficsit Companion</h1>

<p align="center">A node-based Satisfactory production planner</p>

<div align="center">
    <img src="https://github.com/adepierre/ficsit-companion/actions/workflows/build.yaml/badge.svg" alt="Build status">
    <a href="https://discord.gg/JntZTZehQB" target="_blank"><img src="https://badgen.net/badge/icon/Discord?icon=discord&label" alt="Discord link"></a>
    <a href="https://adepierre.github.io/ficsit-companion/" target="_blank"><img src="https://badgen.net/badge/%F0%9F%8C%90/Online%20version/blue" alt="Online version"></a>
    <img src="https://badgen.net/badge/license/MIT/orange" alt="License MIT">
</div>

<p align="center" style="margin-bottom: 0px !important;">
  <img width="800" src="https://github.com/user-attachments/assets/8ff8ac2e-513b-4cf1-b10a-b65937dc81ce" alt="ficsit companion illustration gif" align="center">
</p>

Ficsit Companion is a node based production planner for Satisfactory. It's still in beta version and very experimental. You can test the online version [here](https://adepierre.github.io/ficsit-companion/) or download a standalone desktop version from the [release page](https://github.com/adepierre/ficsit-companion/releases/latest).

## Plans and ideas

This is a partial list of what *could* eventually be integrated into Ficsit Companion in the future. Please keep in mind that it is mostly developed by one person on free time (and my factory also needs to grow), so there is no timeline and some features may end up never being implemented.

<details>
<summary>UI/UX improvements</summary>

Usually, I tend to favor functionnality before look (please don't judge my shoebox factories). As a consequence the UI and web version are currently not very pretty and lots of thing could be improved visually for a better experience.
</details>

<details>
<summary>Power information</summary>

Adding power information to the left pannel is not as simple as that. The first issue is the Particle Accelerator (and potentially other new buildings coming in future updates), that has varying power requirements based on time and recipes used. The other thing to think about is overclocking: assuming you need 2.4 Constructors, you can either build
- 1 Constructor overclocked to 240%, for a total of 12.73 MW
- 3 Constructors, one of them being underclocked to 40%, for a total of 9.19 MW
- 3 Constructors, all underclocked to 80%, for a total of 8.93 MW
- 6 Constructors, all underclocked to 40% (for example if you plan to upgrade once you get higher tier belts), for a total of 7.15 MW

In this case, it is not clear which option should be displayed in the production summary. I'd say both options 2 and 3 could be interesting to have, but would like to hear feedback about that.
</details>

<details>
<summary>Pin reordering</summary>

Inside a node, in/out pins order is irrelevant. However, being able to reorder them could be helpful to get cleaner layouts with less link crossing overall.
</details>

<details>
<summary>Multi-pins merger/splitter</summary>

Currently, merger and splitter nodes are limited to two in/out pins only, forcing to chain them. Having a button or an option to add (remove?) pins would simplify some graphs. I'm not sure yet what would be a "good" UX for that. It also requires to rethink a bit the propagation algorithm as it currently assumes there are always only two in/out pins.
</details>

<details>
<summary>Better cycle support</summary>

Currently production cycles (such as recycled plastic/rubber) are still quite buggy and sometimes require users to manually update rates to balance things out. I am not sure there is a "perfect" solution here, but improving support for circular productions would definitely be a good thing.
</details>

<details>
<summary>Node groups/blueprints</summary>

Creating large production chains can pretty quickly lead to cluttered environment. It could be useful to have the ability to create "custom" nodes (for example by collapsing a set of existing nodes) with different inputs and outputs. Saving/reusing these nodes as "blueprints" could also be handy. Issues may arise to properly keep in sync inputs and outputs of such nodes though.
</details>

<details>
<summary>Automatic tests</summary>

This is more a developper thing, but adding automatic tests using the [ImGui Test Engine](https://github.com/ocornut/imgui_test_engine) would be very helpful to speed up development and debugging, avoid regressions etc...
</details>

<details>
<summary>In-game integration</summary>

I don't know much about Satisfactory mods, but as this is a C++ project using ImGui, I think it may be possible to have it integrated directly in game as a mod/part of a mod. At the moment this is just an idea though, and it would probably require a lot of tweaking to have it working.
</details>

<details>
<summary>Node placing/sorting</summary>

Having a button to automatically sort/place all the nodes on screen to minimize link crossing would be very useful. Not sure how to implement it properly yet though.
</details>

<details>
<summary>Automatic planning</summary>

I'm not a fan of fully automatic tools that can generate full optimized production chains, as I think they take away part of the fun of the planning phase. That being said, it could still be useful to add an option to expand a pin with a full production chain. Development-wise, it may be quite hard to implement from scratch, but using a tool like lpsolve could be fairly easy, assuming it's possible to translate Satisfactory constraints into the chosen solver language.
</details>

<details>
<summary>Other games support</summary>

The main concept of node editor production planner is not specific to Satisfactory and could be extended to other similar games (Factorio, Dyson Sphere Program...). The only requirement would be the possibility to generate a similar recipes json file (and potentially have images for the items).
</details>

<details>
<summary>Image exporter</summary>

Currently, if you want to visualize a previously generated production chain, you need to reopen it inside Ficsit Companion. It means each node takes a lot of space with all the pins. It would be helpful to have an option to export it to a more compact format, potentially an image, where you can easily see everything at a glance. This could either be integrated directly in the app or as a side script reading from a saved file.
</details>


## Building

```bash
git clone https://github.com/adepierre/ficsit-companion.git
cd ficsit-companion
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -S .. -B .
cmake --build . --config Release
```

To build the web version locally you'll need to first install emscripten. Web version will be located inside ``build/web``
```bash
git clone https://github.com/adepierre/ficsit-companion.git
cd ficsit-companion
mkdir build
cd build
/path/to/emscripten/emsdk activate latest
emcmake cmake -DCMAKE_BUILD_TYPE=Release -S .. -B .
cmake --build . --config Release
```

## Updating

The recipes are currently up to date with version 0.8.3.3 of the game. To update to a different version, one can use the [provided script](scripts/data_extractor.py). It requires having the Docs.json file provided in the game files as well as item icons extracted from the game (you can refer to [this tutorial](https://docs.ficsit.app/satisfactory-modding/latest/Development/ExtractGameFiles.html) to do so).

## Credits

Thanks to the amazing ocornut and thedmd for their libraries ([Dear ImGui](https://github.com/ocornut/imgui/) and [ImGui Node Editor](https://github.com/thedmd/imgui-node-editor) respectively).

## Disclaimer

Ficsit Companion is not an official Ficsit nor Coffee Stain Studios product. All images comes from Satisfactory and are Coffee Stain Studios intellectual property.
