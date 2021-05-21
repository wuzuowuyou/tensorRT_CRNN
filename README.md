# tensorRT_CRNN

For the Pytorch implementation, you can refer to [wuzuowuyou/CRNN_Chinese_Characters_res18_senet](https://github.com/wuzuowuyou/CRNN_Chinese_Characters_res18_senet)

## How to run
```
1. generate wts file. from pytorch
python CRNN_Chinese_Characters_res18_senet/demo_get_wts_for_trt.py
// a file 'refinedet.wts' will be generated.

2. build tensorRT_CRNN and run or Using clion to open a project(recommend)
Configuration file in configure.h
You need configure your own paths and modes(SERIALIZE or INFER)
Detailed information reference configure.h
mkdir build
cd build
cmake ..
make
```

## dependence
```
TensorRT-7.2.3.4
OpenCV 3.4.6
libtorch 1.7
```

## More Information
[wuzuowuyou/CRNN_Chinese_Characters_res18_senet](https://github.com/wuzuowuyou/CRNN_Chinese_Characters_res18_senet)
[wuzuowuyou/crnn_libtorch](https://github.com/wuzuowuyou/crnn_libtorch)
[wuzuowuyou/tensorRT_CRNN](https://github.com/wuzuowuyou/tensorRT_CRNN)
[wang-xinyu/tensorrtx](https://github.com/wang-xinyu/tensorrtx)
If this repository helps you，please star it. Thanks.
