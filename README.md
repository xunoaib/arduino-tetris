# Arduino Tetris

- Arduino (Nano, Uno, or similar)
- 8x32 LED Matrix (WS2812B)
- NES Controller

# Data Pins

| Component | Pin |
| :--- | :--- |
| LED Data | D5 |
| NES Data | D6 |
| NES Latch | D7 |
| NES Pulse | D8 |


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
