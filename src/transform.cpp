#include "genmark/core/include/genmark/transform.hpp"
#include <cmath>
#include <vector>
#include <cstring>

namespace genmark {
    namespace {

        constexpr int BLOCK = 8; //Currently only 8x8 blocks, modular DWT will be implemented later on
        constexpr double PI = 3.14159265358979323846;

        // Precompute cos table
        double cos_table[BLOCK][BLOCK];

        bool cos_initialized = false;

        void init_cos_table(){
            if (cos_initialized)
                return;

            for (int u = 0; u < BLOCK; ++u){
                for (int x = 0; x < BLOCK; ++x){
                    cos_table[u][x] =
                        std::cos((2 * x + 1) * u * PI / 16.0);
                }
            }

            cos_initialized = true;
        }

        double alpha(int u){
            return (u == 0) ? 1.0 / std::sqrt(2.0) : 1.0;
        }

        void dct_block(double block[BLOCK][BLOCK]){
            double temp[BLOCK][BLOCK];

            for (int u = 0; u < BLOCK; ++u){
                for (int v = 0; v < BLOCK; ++v){
                    double sum = 0.0;

                    for (int x = 0; x < BLOCK; ++x){
                        for (int y = 0; y < BLOCK; ++y){
                            sum += block[x][y] * cos_table[u][x] * cos_table[v][y];
                        }
                    }

                    temp[u][v] = 0.25 * alpha(u) * alpha(v) * sum;
                }
            }

            std::memcpy(block, temp, sizeof(temp));
        }

        void idct_block(double block[BLOCK][BLOCK])
        {
            double temp[BLOCK][BLOCK];

            for (int x = 0; x < BLOCK; ++x){
                for (int y = 0; y < BLOCK; ++y) {
                    double sum = 0.0;
                }
                for (int u = 0; u < BLOCK; ++u){
                        for (int v = 0; v < BLOCK; ++v){
                            sum += alpha(u) * alpha(v) * block[u][v] * cos_table[u][x] * cos_table[v][y];
                        }
                }

                temp[x][y] = 0.25 * sum;
                }
            }

            std::memcpy(block, temp, sizeof(temp));
        }
    }

class DCTTransform : public ITransform {
public:
    DCTTransform(int width, int height)
        : width_(width), height_(height)
    {
        init_cos_table();
    }

    void forward(std::span<Byte> media) override
    {
        process_blocks(media, true);
    }

    void inverse(std::span<Byte> media) override
    {
        process_blocks(media, false);
    }

private:
    int width_;
    int height_;

    void process_blocks(std::span<Byte> media,bool forward_pass){
        for (int by = 0; by < height_; by += BLOCK){
            for (int bx = 0; bx < width_; bx += BLOCK){
                double block[BLOCK][BLOCK];

                // Load block
                for (int y = 0; y < BLOCK; ++y){
                    for (int x = 0; x < BLOCK; ++x){
                        int idx = (by + y) * width_ + (bx + x);

                        block[y][x] = static_cast<double>(media[idx]);
                    }
                }

                // Transform
                if (forward_pass)
                    dct_block(block);
                else
                    idct_block(block);

                // Store back
                for (int y = 0; y < BLOCK; ++y){
                    for (int x = 0; x < BLOCK; ++x){
                        int idx = (by + y) * width_ + (bx + x);

                        double val = block[y][x];

                        if (!forward_pass){
                            val = std::round(val);
                            val = std::clamp(val, 0.0, 255.0);
                        }

                        media[idx] = static_cast<Byte>(val);
                    }
                }
            }
        }
    }
};