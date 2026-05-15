# Hardware upgrades for Casio FZ-1 / FZ-10m / FZ-20m.

This repository is the collection of upgrades for an amazing Casio FZ-series of samplers.

## FZDUMP2026
Is the new version of the original FZDUMP app for DOS that allowed data transfer between FZ-1 and PC. I has been completelly reworked in 2026 (30 years after the original code) to run on "modern" Windows - anything from and including Windows 98 SE.
It was purposely designed to use the parallel port, not rely on any mmicrocontroller, and just initiate a transfer using a user space library to interface with LPT port. This required specific tricks in the code, since the original code used DOS interrupts.
The main motivation is to keep it legacy looking.

Developed by Deftaudio.

## 1MB FZ-1 Memory Upgrade board
Memory Expansion is designed to add an extra 1MB of sample memory for Casio FZ-1 (only keyboard version has an expansion slot). It replicates the1987 circuitry by leveraging off the shelf 256Kx1 DRAM chips which are easily obtainable.  

Beginner soldering skills are required. The kit consists of thru hole parts only, easy to solder. It includes quality IC sockets (round pin hole ) for all components for easy installation or replacement.  

Plug and play. Doesn't require opening FZ-1. The memory expansion slot is located on the back of the instrument.  

Works with any 256Kx1 DRAM (80ns or better) such as MB81256-80, HY53C256LS-80, KM41C256P-7, MN41256A-08 and many other equivalents.

More info on the [Product page](https://deftaudio.com/store/tproduct/857848623614-casio-fz-1-1mb-memory-expansion).

## FZ-1 OLED Upgrade
This is a copy of the open source design files by Dmitrins who shared it on [Yamahamusician](https://yamahamusicians.com/forum/viewtopic.php?t=10666&start=240)  just to preserve it.

Deftaudio doesn't sell OLED kits, by we're happy to help if you got stuck with the design.

## Flashfloppy Floppy Emulator
Just a set of configuration files and test images to get Casio FZ-1 running with Flashfloppy.   
