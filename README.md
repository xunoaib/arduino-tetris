# Arduino Tetris

- Arduino
- 8x32 LED Matrix (WS2812B)
- NES Controller

# NES Controller Wiring

[NES Controller Reference](https://web.archive.org/web/20160301181841/http://www.mit.edu/~tarvizo/nes-controller.html)

```
          +----> Power  (5V)
          |
5 +---------+  7    
  | x  x  o   \     
  | o  o  o  o |    
4 +------------+ 1  
    |  |  |  |
    |  |  |  +-> Ground (GND)
    |  |  +----> Pulse  (i.e. D8)
    |  +-------> Latch  (i.e. D7)
    +----------> Data   (i.e. D6)
```
