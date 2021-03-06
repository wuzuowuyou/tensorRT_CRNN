#ifndef YOLOV5_COMMON_H_
#define YOLOV5_COMMON_H_

#include <iostream>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>
#include <opencv2/opencv.hpp>
#include "dirent.h"
#include "NvInfer.h"
#include <chrono>

#define CHECK(status) \
    do\
    {\
        auto ret = (status);\
        if (ret != 0)\
        {\
            std::cerr << "Cuda failure: " << ret << std::endl;\
            abort();\
        }\
    } while (0)

using namespace nvinfer1;

// Load weights from files shared with TensorRT samples.
// TensorRT weight files have a simple space delimited format:
// [type] [size] <data x size in hex>
std::map<std::string, Weights> loadWeights(const std::string file)
{
    std::cout << "Loading weights: " << file << std::endl;
    std::map<std::string, Weights> weightMap;

    // Open weights file
    std::ifstream input(file);
    assert(input.is_open() && "Unable to load weight file.");

    // Read number of weight blobs
    int32_t count;
    input >> count;
    assert(count > 0 && "Invalid weight map file.");

    while (count--)
    {
        Weights wt{DataType::kFLOAT, nullptr, 0};
        uint32_t size;

        // Read name and type of blob
        std::string name;
        input >> name >> std::dec >> size;
        wt.type = DataType::kFLOAT;

        // Load blob
        uint32_t* val = reinterpret_cast<uint32_t*>(malloc(sizeof(val) * size));
        for (uint32_t x = 0, y = size; x < y; ++x)
        {
            input >> std::hex >> val[x];
        }
        wt.values = val;

        wt.count = size;
        weightMap[name] = wt;
    }

    return weightMap;
}

IScaleLayer* addBatchNorm2d(INetworkDefinition *network, std::map<std::string, Weights>& weightMap, ITensor& input, std::string lname, float eps) {

    if (weightMap.count(lname + ".weight") == 0)
        std::cout << "no key: " <<lname + ".weight" << std::endl;

    if (weightMap.count(lname + ".bias") == 0)
        std::cout << "no key: " <<lname + ".bias" << std::endl;

    if (weightMap.count(lname + ".running_mean") == 0)
        std::cout << "no key: " <<lname + ".running_mean" << std::endl;

    if (weightMap.count(lname + ".running_var") == 0)
        std::cout << "no key: " <<lname + ".running_var" << std::endl;


    float *gamma = (float*)weightMap[lname + ".weight"].values;
    float *beta = (float*)weightMap[lname + ".bias"].values;
    float *mean = (float*)weightMap[lname + ".running_mean"].values;
    float *var = (float*)weightMap[lname + ".running_var"].values;
    int len = weightMap[lname + ".running_var"].count;
    std::cout << "len " << len << std::endl;

    float *scval = reinterpret_cast<float*>(malloc(sizeof(float) * len));
    for (int i = 0; i < len; i++) {
        scval[i] = gamma[i] / sqrt(var[i] + eps);
    }
    Weights scale{DataType::kFLOAT, scval, len};

    float *shval = reinterpret_cast<float*>(malloc(sizeof(float) * len));
    for (int i = 0; i < len; i++) {
        shval[i] = beta[i] - mean[i] * gamma[i] / sqrt(var[i] + eps);
    }
    Weights shift{DataType::kFLOAT, shval, len};

    float *pval = reinterpret_cast<float*>(malloc(sizeof(float) * len));
    for (int i = 0; i < len; i++) {
        pval[i] = 1.0;
    }
    Weights power{DataType::kFLOAT, pval, len};

    weightMap[lname + ".scale"] = scale;
    weightMap[lname + ".shift"] = shift;
    weightMap[lname + ".power"] = power;
    IScaleLayer* scale_1 = network->addScale(input, ScaleMode::kCHANNEL, shift, scale, power);
    assert(scale_1);
    return scale_1;
}

IActivationLayer* basicBlock(INetworkDefinition *network, std::map<std::string, Weights>& weightMap, ITensor& input, int inch, int outch, int stride, std::string lname) {
    Weights emptywts{DataType::kFLOAT, nullptr, 0};

    if (weightMap.count(lname + "conv1.weight") == 0)
        std::cout << "no key: " <<lname + "conv1.weight" << std::endl;

    if (weightMap.count(lname + "conv2.weight") == 0)
        std::cout << "no key: " <<lname + "conv2.weight" << std::endl;


    IConvolutionLayer* conv1 = network->addConvolutionNd(input, outch, DimsHW{3, 3}, weightMap[lname + "conv1.weight"], emptywts);
    assert(conv1);
    conv1->setStrideNd(DimsHW{stride, stride});
    conv1->setPaddingNd(DimsHW{1, 1});

    IScaleLayer* bn1 = addBatchNorm2d(network, weightMap, *conv1->getOutput(0), lname + "bn1", 1e-5);

    IActivationLayer* relu1 = network->addActivation(*bn1->getOutput(0), ActivationType::kRELU);
    assert(relu1);

    IConvolutionLayer* conv2 = network->addConvolutionNd(*relu1->getOutput(0), outch, DimsHW{3, 3}, weightMap[lname + "conv2.weight"], emptywts);
    assert(conv2);
    conv2->setPaddingNd(DimsHW{1, 1});

    IScaleLayer* bn2 = addBatchNorm2d(network, weightMap, *conv2->getOutput(0), lname + "bn2", 1e-5);

    IElementWiseLayer* ew1;
    if (inch != outch) {
        if (weightMap.count(lname + "downsample.0.weight") == 0)
            std::cout << "no key: " <<lname + "downsample.0.weight" << std::endl;

        IConvolutionLayer* conv3 = network->addConvolutionNd(input, outch, DimsHW{1, 1}, weightMap[lname + "downsample.0.weight"], emptywts);
        assert(conv3);
        conv3->setStrideNd(DimsHW{stride, stride});
        IScaleLayer* bn3 = addBatchNorm2d(network, weightMap, *conv3->getOutput(0), lname + "downsample.1", 1e-5);
        ew1 = network->addElementWise(*bn3->getOutput(0), *bn2->getOutput(0), ElementWiseOperation::kSUM);
    } else {
        ew1 = network->addElementWise(input, *bn2->getOutput(0), ElementWiseOperation::kSUM);
    }
    IActivationLayer* relu2 = network->addActivation(*ew1->getOutput(0), ActivationType::kRELU);
    assert(relu2);
    return relu2;
}

IActivationLayer* basicBlock_2(INetworkDefinition *network, std::map<std::string, Weights>& weightMap, ITensor& input, int inch, int outch, int stride, std::string lname,bool b_is_1=false) {
    Weights emptywts{DataType::kFLOAT, nullptr, 0};
    int stride_x = 2;
    int stride_y = 1;

    if(false == b_is_1)
    {
        stride_x = 1;
    }

    if (weightMap.count(lname + "conv1.weight") == 0)
        std::cout << "no key: " <<lname + "conv1.weight" << std::endl;

    if (weightMap.count(lname + "conv2.weight") == 0)
        std::cout << "no key: " <<lname + "conv2.weight" << std::endl;

    IConvolutionLayer* conv1 = network->addConvolutionNd(input, outch, DimsHW{3, 3}, weightMap[lname + "conv1.weight"], emptywts);
    assert(conv1);
    conv1->setStrideNd(DimsHW{stride_x, stride_y});
    conv1->setPaddingNd(DimsHW{1, 1});

    IScaleLayer* bn1 = addBatchNorm2d(network, weightMap, *conv1->getOutput(0), lname + "bn1", 1e-5);

    IActivationLayer* relu1 = network->addActivation(*bn1->getOutput(0), ActivationType::kRELU);
    assert(relu1);

    IConvolutionLayer* conv2 = network->addConvolutionNd(*relu1->getOutput(0), outch, DimsHW{3, 3}, weightMap[lname + "conv2.weight"], emptywts);
    assert(conv2);
    conv2->setPaddingNd(DimsHW{1, 1});

    IScaleLayer* bn2 = addBatchNorm2d(network, weightMap, *conv2->getOutput(0), lname + "bn2", 1e-5);

    IElementWiseLayer* ew1;
    if (inch != outch) {
        if (weightMap.count(lname + "downsample.0.weight") == 0)
            std::cout << "no key: " <<lname + "downsample.0.weight" << std::endl;

        IConvolutionLayer* conv3 = network->addConvolutionNd(input, outch, DimsHW{1, 1}, weightMap[lname + "downsample.0.weight"], emptywts);
        assert(conv3);
        conv3->setStrideNd(DimsHW{stride_x, stride_y});
        IScaleLayer* bn3 = addBatchNorm2d(network, weightMap, *conv3->getOutput(0), lname + "downsample.1", 1e-5);
        ew1 = network->addElementWise(*bn3->getOutput(0), *bn2->getOutput(0), ElementWiseOperation::kSUM);
    } else {
        ew1 = network->addElementWise(input, *bn2->getOutput(0), ElementWiseOperation::kSUM);
    }
    IActivationLayer* relu2 = network->addActivation(*ew1->getOutput(0), ActivationType::kRELU);
    assert(relu2);
    return relu2;
}

void splitLstmWeights(std::map<std::string, Weights>& weightMap, std::string lname) {
    int weight_size = weightMap[lname].count;
    for (int i = 0; i < 4; i++) {
        Weights wt{DataType::kFLOAT, nullptr, 0};
        wt.count = weight_size / 4;
        float *val = reinterpret_cast<float*>(malloc(sizeof(float) * wt.count));
        memcpy(val, (float*)weightMap[lname].values + wt.count * i, sizeof(float) * wt.count);
        wt.values = val;
        weightMap[lname + std::to_string(i)] = wt;
    }
}

ILayer* addLSTM(INetworkDefinition *network, std::map<std::string, Weights>& weightMap, ITensor& input, int nHidden, std::string lname) {
    splitLstmWeights(weightMap, lname + ".weight_ih_l0");
    splitLstmWeights(weightMap, lname + ".weight_hh_l0");
    splitLstmWeights(weightMap, lname + ".bias_ih_l0");
    splitLstmWeights(weightMap, lname + ".bias_hh_l0");
    splitLstmWeights(weightMap, lname + ".weight_ih_l0_reverse");
    splitLstmWeights(weightMap, lname + ".weight_hh_l0_reverse");
    splitLstmWeights(weightMap, lname + ".bias_ih_l0_reverse");
    splitLstmWeights(weightMap, lname + ".bias_hh_l0_reverse");
    Dims dims = input.getDimensions();
    std::cout << "lstm input shape: " << dims.nbDims << " [" << dims.d[0] << " " << dims.d[1] << " " << dims.d[2] << "]"<< std::endl;
    auto lstm = network->addRNNv2(input, 1, nHidden, dims.d[1], RNNOperation::kLSTM);
    lstm->setDirection(RNNDirection::kBIDIRECTION);
    lstm->setWeightsForGate(0, RNNGateType::kINPUT, true, weightMap[lname + ".weight_ih_l00"]);
    lstm->setWeightsForGate(0, RNNGateType::kFORGET, true, weightMap[lname + ".weight_ih_l01"]);
    lstm->setWeightsForGate(0, RNNGateType::kCELL, true, weightMap[lname + ".weight_ih_l02"]);
    lstm->setWeightsForGate(0, RNNGateType::kOUTPUT, true, weightMap[lname + ".weight_ih_l03"]);

    lstm->setWeightsForGate(0, RNNGateType::kINPUT, false, weightMap[lname + ".weight_hh_l00"]);
    lstm->setWeightsForGate(0, RNNGateType::kFORGET, false, weightMap[lname + ".weight_hh_l01"]);
    lstm->setWeightsForGate(0, RNNGateType::kCELL, false, weightMap[lname + ".weight_hh_l02"]);
    lstm->setWeightsForGate(0, RNNGateType::kOUTPUT, false, weightMap[lname + ".weight_hh_l03"]);

    lstm->setBiasForGate(0, RNNGateType::kINPUT, true, weightMap[lname + ".bias_ih_l00"]);
    lstm->setBiasForGate(0, RNNGateType::kFORGET, true, weightMap[lname + ".bias_ih_l01"]);
    lstm->setBiasForGate(0, RNNGateType::kCELL, true, weightMap[lname + ".bias_ih_l02"]);
    lstm->setBiasForGate(0, RNNGateType::kOUTPUT, true, weightMap[lname + ".bias_ih_l03"]);

    lstm->setBiasForGate(0, RNNGateType::kINPUT, false, weightMap[lname + ".bias_hh_l00"]);
    lstm->setBiasForGate(0, RNNGateType::kFORGET, false, weightMap[lname + ".bias_hh_l01"]);
    lstm->setBiasForGate(0, RNNGateType::kCELL, false, weightMap[lname + ".bias_hh_l02"]);
    lstm->setBiasForGate(0, RNNGateType::kOUTPUT, false, weightMap[lname + ".bias_hh_l03"]);

    lstm->setWeightsForGate(1, RNNGateType::kINPUT, true, weightMap[lname + ".weight_ih_l0_reverse0"]);
    lstm->setWeightsForGate(1, RNNGateType::kFORGET, true, weightMap[lname + ".weight_ih_l0_reverse1"]);
    lstm->setWeightsForGate(1, RNNGateType::kCELL, true, weightMap[lname + ".weight_ih_l0_reverse2"]);
    lstm->setWeightsForGate(1, RNNGateType::kOUTPUT, true, weightMap[lname + ".weight_ih_l0_reverse3"]);

    lstm->setWeightsForGate(1, RNNGateType::kINPUT, false, weightMap[lname + ".weight_hh_l0_reverse0"]);
    lstm->setWeightsForGate(1, RNNGateType::kFORGET, false, weightMap[lname + ".weight_hh_l0_reverse1"]);
    lstm->setWeightsForGate(1, RNNGateType::kCELL, false, weightMap[lname + ".weight_hh_l0_reverse2"]);
    lstm->setWeightsForGate(1, RNNGateType::kOUTPUT, false, weightMap[lname + ".weight_hh_l0_reverse3"]);

    lstm->setBiasForGate(1, RNNGateType::kINPUT, true, weightMap[lname + ".bias_ih_l0_reverse0"]);
    lstm->setBiasForGate(1, RNNGateType::kFORGET, true, weightMap[lname + ".bias_ih_l0_reverse1"]);
    lstm->setBiasForGate(1, RNNGateType::kCELL, true, weightMap[lname + ".bias_ih_l0_reverse2"]);
    lstm->setBiasForGate(1, RNNGateType::kOUTPUT, true, weightMap[lname + ".bias_ih_l0_reverse3"]);

    lstm->setBiasForGate(1, RNNGateType::kINPUT, false, weightMap[lname + ".bias_hh_l0_reverse0"]);
    lstm->setBiasForGate(1, RNNGateType::kFORGET, false, weightMap[lname + ".bias_hh_l0_reverse1"]);
    lstm->setBiasForGate(1, RNNGateType::kCELL, false, weightMap[lname + ".bias_hh_l0_reverse2"]);
    lstm->setBiasForGate(1, RNNGateType::kOUTPUT, false, weightMap[lname + ".bias_hh_l0_reverse3"]);
    return lstm;
}

int read_files_in_dir(const char *p_dir_name, std::vector<std::string> &file_names) {
    DIR *p_dir = opendir(p_dir_name);
    if (p_dir == nullptr) {
        return -1;
    }

    struct dirent* p_file = nullptr;
    while ((p_file = readdir(p_dir)) != nullptr) {
        if (strcmp(p_file->d_name, ".") != 0 &&
            strcmp(p_file->d_name, "..") != 0) {
            //std::string cur_file_name(p_dir_name);
            //cur_file_name += "/";
            //cur_file_name += p_file->d_name;
            std::string cur_file_name(p_file->d_name);
            file_names.push_back(cur_file_name);
        }
    }
    closedir(p_dir);
    return 0;
}

#endif