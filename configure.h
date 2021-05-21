/*---------------------------configuration parameter---------------------------------*/
//#define SERIALIZE     //由wts生成engine打开这行注释
#define INFER           //engine生成好，跑图片测试打开这行注释

static const int INPUT_H = 32;
static const int INPUT_W = 320;

static const int TIME_STEP = 80;
static const int NUM_CLASS = 6741;

std::vector<float> v_mean = {0.406,0.224,0.229};
std::vector<float> v_std = {0.406,0.456,0.485};


// SERIALIZE 序列化的时候需要指定path_wts  path_save_engin
std::string path_wts = "./crnn_res18_20210520.wts";
std::string path_save_engin = "./crnn_resnet18.engine";


//INFER 推理的时候需要指定path_read_engin
std::string path_read_engin = "./crnn_resnet18.engine";

//测试图片文件夹
const std::string dir_img = "./data/test/pic/";


#define FP32  // FP32 FP16     comment out this if want to use FP32
#define DEVICE 0  // GPU id

const char* INPUT_BLOB_NAME = "data";
const char* OUTPUT_BLOB_NAME = "out";
/*---------------------------configuration parameter---------------------------------*/
