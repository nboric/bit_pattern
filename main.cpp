#include <fstream>
#include <iostream>

#define N_BYTES 10000000
#define BATCH_SIZE 1000

class Method
{
protected:
    const unsigned char pattern{0b110};

public:
    int total_count{0};
    int64_t total_time{0};
    virtual ~Method() = default;
    virtual int pattern_match(unsigned char sample) = 0;
    virtual std::string getName() = 0;
};

class StateMachine final : public Method
{
    int pos{0};

public:
    std::string getName() override { return "StateMachine"; }

    int pattern_match(const unsigned char sample) override
    {
        int counter = 0;
        for (int i = 7; i >= 0; i--)
        {
            const unsigned char bit = sample & (1 << i);
            switch (pos)
            {
            case 0:
            case 1:
                if (bit)
                {
                    // transition to first or second 1
                    pos++;
                }
                else
                {
                    // restart
                    pos = 0;
                }
                break;
            case 2:
                if (bit)
                {
                    // we saw 111, therefore we still have two 1's, stay in same pos
                }
                else
                {
                    // match
                    counter++;
                    pos = 0;
                }
                break;
            default:
                // bug
                break;
            }
        }
        total_count += counter;
        return counter;
    }
};

class SlidingBitmask final : public Method
{
    unsigned char prev{0};

public:
    std::string getName() override { return "SlidingBitmask"; }

    int pattern_match(const unsigned char sample) override
    {
        int counter = 0;
        const unsigned short combined_samples = (static_cast<unsigned short>(prev) << 8) | sample;
        // we need two bits from previous sample, and we look at three bits at a time
        // [... 9 8][7 6 5 4 3 2 1 0]
        // so first time we shift 7 times to get:
        // 9 8 7]
        // last time we shift 0:
        // 2 1 0]
        for (int i = 7; i >= 0; i--)
        {
            if (((combined_samples >> i) & 0x07) == pattern)
            {
                counter++;
            }
        }
        total_count += counter;
        prev = sample;
        return counter;
    }
};

class Lut final : public Method
{
    unsigned char prev{0};
    int count_lut[1024]{}; // 10 bits

public:
    std::string getName() override { return "LUT"; }

    Lut()
    {
        unsigned short combined_samples = 0;
        // build lut
        while (combined_samples < std::size(count_lut))
        {
            SlidingBitmask method2;
            // we reuse method 2, first pass MSB
            int counter = method2.pattern_match(combined_samples >> 8);
            // now pass LSB:
            counter += method2.pattern_match(combined_samples & 0xFF);
            count_lut[combined_samples] = counter;
            combined_samples++;
        }
    }

    int pattern_match(const unsigned char sample) override
    {
        int counter = 0;
        const unsigned short combined_samples = ((static_cast<unsigned short>(prev) << 8) | sample) & 0x3FF;
        counter = count_lut[combined_samples];
        total_count += counter;
        prev = sample;
        return counter;
    }
};

int main()
{
    std::vector<std::unique_ptr<Method>> methods;
    methods.push_back(std::make_unique<StateMachine>());
    methods.push_back(std::make_unique<SlidingBitmask>());
    methods.push_back(std::make_unique<Lut>());

    // this is just so that we can precalculate the stream and measure time accurately by calling each method many times,
    // instead of measuring every entry into the method, which would be too imprecise
    // the methods themselves work with streamed data
    for (int batch = 0; batch < N_BYTES / BATCH_SIZE; batch++)
    {
        std::ifstream devRandom("/dev/random", std::ios::binary);
        std::array<unsigned char, BATCH_SIZE> samples{};
        devRandom.read(reinterpret_cast<char*>(samples.data()), BATCH_SIZE);

        for (const auto& method : methods)
        {
            std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
            for (int i = 0; i < BATCH_SIZE; i++)
            {
                method->pattern_match(samples[i]);
            }
            std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
            method->total_time += std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
        }
    }

    for (const auto& method : methods)
    {
        std::cout << "Method " << method->getName() << " total count: " << method->total_count << ", time: "
            << method->total_time / 1000. << " ms" << std::endl;
    }
    return 0;
}
