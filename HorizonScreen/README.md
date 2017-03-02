# HorizonScreen
slave application for HorizonM's screen streaming feature

`HorizonScreen` is licensed under the `MIT` license. See `LICENSE` for details.


## Current features
* framebuffer rendering received from HorizonM


## Building
- install gcc and g++ 5.x
- instlall libSDL2-dev 2.0.5+
- `make`

## Getting started
- Get the IP address of your 3DS
- Launch HorizonM
- Run `out/PC/HorizonScreen-PC.elf <IP address of your 3DS>`
  - Example: `out/PC/HorizonScreen-PC.elf 10.0.0.103`
- If HorizonScreen freezes, terminate it either by pressing `CTRL-C`/`STRG-C` in the terminal, or send the application a `SIGTERM` or `SIGKILL`


###### ~~And if you order now, you'll get a `SIGSEGV` or `SIGFPT` for FREE!~~
                                                          