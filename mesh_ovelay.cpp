#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace cv;
using namespace std;


const string INPUT_DIR = "/home/joeld/VectorBlox-SDK/tutorials/onnx/freshly_made_model/test-dataset";
const string OUTPUT_DIR = "/home/joeld/VectorBlox-SDK/tutorials/onnx/freshly_made_model/mesh-data";


void normalize_percentile(Mat& src) {
    if (src.empty()) return;

    std::vector<float> data;
    data.assign((float*)src.datastart, (float*)src.dataend);
    
    size_t n = data.size();
    size_t idx_lo = (size_t)(0.03 * n); // 3rd percentile
    size_t idx_hi = (size_t)(0.97 * n); // 97th percentile

    std::nth_element(data.begin(), data.begin() + idx_lo, data.end());
    float lo = data[idx_lo];

    std::nth_element(data.begin(), data.begin() + idx_hi, data.end());
    float hi = data[idx_hi];

    if (hi - lo < 1e-6f) {
        src = Mat::zeros(src.size(), src.type());
        return;
    }

    src = (src - lo) / (hi - lo);
    threshold(src, src, 1.0, 1.0, THRESH_TRUNC); // clip > 1 to 1
    threshold(src, src, 0.0, 0.0, THRESH_TOZERO); // clip < 0 to 0 (implicit in logic usually, but safe)
}

Mat get_box_mask(const Mat& img) {
    Mat hsv;
    cvtColor(img, hsv, COLOR_BGR2HSV);

    Mat blue, green, white;
    inRange(hsv, Scalar(90, 40, 40), Scalar(135, 255, 255), blue);
    inRange(hsv, Scalar(35, 40, 40), Scalar(85, 255, 255), green);
    inRange(hsv, Scalar(0, 0, 200), Scalar(180, 40, 255), white);

    Mat bg;
    bitwise_or(blue, green, bg);
    bitwise_or(bg, white, bg);

    Mat fg;
    bitwise_not(bg, fg);

    Mat k = getStructuringElement(MORPH_ELLIPSE, Size(9, 9));
    morphologyEx(fg, fg, MORPH_OPEN, k);
    morphologyEx(fg, fg, MORPH_CLOSE, k);

    Mat labels, stats, centroids;
    int num_labels = connectedComponentsWithStats(fg, labels, stats, centroids, 8, CV_32S);

    if (num_labels <= 1) {
        return fg;
    }

    int max_area = -1;
    int largest_label = -1;
    for (int i = 1; i < num_labels; i++) {
        int area = stats.at<int>(i, CC_STAT_AREA);
        if (area > max_area) {
            max_area = area;
            largest_label = i;
        }
    }

    Mat mask = (labels == largest_label);
    mask.convertTo(mask, CV_8U, 255.0); // Convert boolean/int mask to 0-255 grayscale
    return mask;
}

std::pair<Mat, Mat> compute_deformation_field(const Mat& img) {
    Mat gray;
    cvtColor(img, gray, COLOR_BGR2GRAY);
    gray.convertTo(gray, CV_32F, 1.0 / 255.0);

    // d=7, sigmaColor=0.08, sigmaSpace=7
    Mat smooth;
    bilateralFilter(gray, smooth, 7, 0.08, 7);

    Mat lap;
    Laplacian(smooth, lap, CV_32F, 3);
    Mat sharpen = smooth - 0.6 * lap;

    Mat gx, gy;
    Scharr(sharpen, gx, CV_32F, 1, 0);
    Scharr(sharpen, gy, CV_32F, 0, 1);
    
    Mat edges;
    magnitude(gx, gy, edges);
    sqrt(edges, edges); // Python code: np.sqrt(gx**2 + gy**2) -> magnitude does sqrt(x^2+y^2). 

    // Normalize sharpen
    Mat sharpen_n = sharpen.clone();
    normalize_percentile(sharpen_n);

    // Normalize edges
    Mat edges_n = edges.clone();
    normalize_percentile(edges_n);

    // Combine
    Mat deform = 0.65 * sharpen_n + 0.35 * edges_n;
    normalize_percentile(deform);

    return {deform, sharpen_n};
}


Scalar box_color(float v) {
    if (v < 0.5f) {
        float t = v / 0.5f;
        return Scalar(255 * (1 - t), 0, 255 * t);
    } else {
        float t = (v - 0.5f) / 0.5f;
        return Scalar(255, 255 * (1 - t), 0);
    }
}

Scalar bg_color() {
    return Scalar(0, 200, 0); // BGR
}


Mat draw_mesh(const Mat& sharpen_gray, const Mat& deform, const Mat& box_mask, int spacing = 18, float warp = 30.0f) {
    Mat base_u8;
    sharpen_gray.convertTo(base_u8, CV_8U, 255.0);
    
    Mat out;
    cvtColor(base_u8, out, COLOR_GRAY2BGR);
    addWeighted(out, 0.85, Mat::zeros(out.size(), out.type()), 0, 0, out);

    Mat gx, gy;
    Sobel(deform, gx, CV_32F, 1, 0, 1);
    Sobel(deform, gy, CV_32F, 0, 1, 1);
    gx = gx * 0.5;
    gy = gy * 0.5;

    int h = deform.rows;
    int w = deform.cols;

    auto process_grid = [&](bool is_horizontal) {
        int outer_limit = is_horizontal ? h : w;
        int inner_limit = is_horizontal ? w : h;

        for (int i = 0; i < outer_limit; i += spacing) {
            Point prev_pt(-1, -1);
            bool has_prev = false;

            for (int j = 0; j < inner_limit; j += spacing) {
                int y = is_horizontal ? i : j;
                int x = is_horizontal ? j : i;

                if (x >= w) x = w - 1;
                if (y >= h) y = h - 1;

                float def_val = deform.at<float>(y, x);
                float grad_x = gx.at<float>(y, x);
                float grad_y = gy.at<float>(y, x);

                float strength = warp * (0.25f + 0.75f * def_val);
                
                int px = std::round(x - grad_x * strength);
                int py = std::round(y - grad_y * strength);

                px = std::max(0, std::min(px, w - 1));
                py = std::max(0, std::min(py, h - 1));

                bool is_mask = box_mask.at<uchar>(y, x) > 0;
                Scalar color = is_mask ? box_color(def_val) : bg_color();

                Point cur_pt(px, py);

                if (has_prev) {
                    line(out, prev_pt, cur_pt, color, 1, LINE_AA);
                }

                prev_pt = cur_pt;
                has_prev = true;
            }
        }
    };

    process_grid(true);
    process_grid(false);

    return out;
}

int main() {
    try {
        if (!fs::exists(OUTPUT_DIR)) {
            fs::create_directories(OUTPUT_DIR);
            std::cout << "Created directory: " << OUTPUT_DIR << std::endl;
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error creating directory: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Scanning: " << INPUT_DIR << std::endl;
    
    int count = 0;
    for (const auto& entry : fs::directory_iterator(INPUT_DIR)) {
        std::string path = entry.path().string();
        std::string ext = entry.path().extension().string();
        
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".jpg" && ext != ".png" && ext != ".jpeg") continue;

        std::cout << "Processing: " << entry.path().filename() << "... ";

        Mat img = imread(path);
        if (img.empty()) {
            std::cout << "[FAILED] (Could not load)" << std::endl;
            continue;
        }

        Mat mask = get_box_mask(img);
        
        std::pair<Mat, Mat> res = compute_deformation_field(img);
        Mat deform = res.first;
        Mat sharpen = res.second;

        Mat mesh = draw_mesh(sharpen, deform, mask);

        std::string out_filename = "mesh_" + entry.path().filename().string();
        fs::path out_path = fs::path(OUTPUT_DIR) / out_filename;

        imwrite(out_path.string(), mesh);
        std::cout << "[SAVED] -> " << out_filename << std::endl;
        count++;
    }

    std::cout << "\nDone! Processed " << count << " images." << std::endl;
    return 0;
}
