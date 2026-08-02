# MC-Nature
This code is part of the MeikeChess saga. A series of chess engines.
# What distinguishes Nature from other MC engines?
Nature uses a pre-installed open-source NNUE, as where BravoBlue (in development) uses PyTorch for a self-training NNUE. Think of it as a "DeepBlue" engine.
# Description of MC-Nature
A chess engine written in C++, using an akimbo bullet NNUE net, Native 3·4·5 SyZyGy database, with HCE as a fallback.
The engine is also known as PsiOmicron, named after the greek alphabet components, but mainly, this name is used to represent what generation the engine is, independently of the version.
## Version 2.0
Fixed issues where HCE was most commonly used, because of a bucket failure in the NNUE
Added GitHub repository
Fixed eval issues, noted in middle games and open games.
(dev) Added SPRT and SPSA functionality.
Added time-man.cpp, time-man.h and time-man.o for time management.
## What will be released in version 2.1?
Most likely, we'll be adding native databases, such as an opening book, and slight optimizations.
