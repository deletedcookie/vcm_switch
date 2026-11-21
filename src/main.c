#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "soc/gpio_struct.h"
#include "driver/uart.h"

#include "calibration.h"

int adc2_data;
int adc1_data;
float travel_mm;
static float filtered_data;
float theta;
float z_hall;
float travel_mm_offset;
float theta_median;
float travel_mm_filtered;
uint8_t coil_top_duty;
uint8_t coil_bottom_duty;



// Define the ADC channel and attenuation
#define EXAMPLE_ADC_UNIT     ADC_UNIT_1
#define EXAMPLE_ADC_CHANNEL  ADC_CHANNEL_4 // GPIO 32
#define EXAMPLE_ADC_ATTEN    ADC_ATTEN_DB_12

// Handles for ADC and Calibration
static adc_oneshot_unit_handle_t adc1_handle;


float map_hall_with_lut(float currentRaw);


#define COIL_TIMER              LEDC_TIMER_0
#define COIL_MODE               LEDC_HIGH_SPEED_MODE
#define COIL_DUTY_RES           LEDC_TIMER_8_BIT 
#define COIL_MAX_DUTY           255
#define COIL_FREQUENCY          30000

#define COIL_BOTTOM_CHANNEL     LEDC_CHANNEL_0
#define PIN_COIL_BOTTOM_EN      (22)               

#define COIL_TOP_CHANNEL        LEDC_CHANNEL_1
#define PIN_COIL_TOP_EN         (23)              

// Math helper to convert 0.0 - 100.0% to the corresponding 13-bit value
static uint32_t pct_to_duty(float percent)
{
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;
    return (uint32_t)((percent / 100.0f) * COIL_MAX_DUTY);
}



static void coil_pwm_init(void)
{
    ledc_timer_config_t coil_timer = {
        .speed_mode       = COIL_MODE,
        .timer_num        = COIL_TIMER,
        .duty_resolution  = COIL_DUTY_RES,
        .freq_hz          = COIL_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&coil_timer));

    // Initialize both channels to 0% duty cycle initially
    ledc_channel_config_t coil_top_channel = {
        .speed_mode     = COIL_MODE,
        .channel        = COIL_TOP_CHANNEL,
        .timer_sel      = COIL_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = PIN_COIL_TOP_EN,
        .duty           = 0, 
        .hpoint         = 0,
        .flags.output_invert = 0 
    };
    ESP_ERROR_CHECK(ledc_channel_config(&coil_top_channel));
    //ESP_ERROR_CHECK(gpio_set_direction(PIN_COIL_TOP_EN, GPIO_MODE_DEF_OD));

    ledc_channel_config_t coil_bottom_channel = {
        .speed_mode     = COIL_MODE,
        .channel        = COIL_BOTTOM_CHANNEL,
        .timer_sel      = COIL_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = PIN_COIL_BOTTOM_EN,
        .duty           = 0, 
        .hpoint         = 0,
        .flags.output_invert = 0 
    };
    ESP_ERROR_CHECK(ledc_channel_config(&coil_bottom_channel));
    //ESP_ERROR_CHECK(gpio_set_direction(PIN_COIL_BOTTOM_EN, GPIO_MODE_DEF_OD));
}

#define PIN_BM_A2  (1UL << GPIO_NUM_21)
#define PIN_BM_A1  (1UL << GPIO_NUM_25)
#define PIN_BM_B1  (1UL << GPIO_NUM_26)
#define PIN_BM_B2  (1UL << GPIO_NUM_27)

static const char *TAG = "GPIO_OD_EXAMPLE";
void io_config (void) {
    // 1. Define the configuration structure
    gpio_config_t io_conf = {
        .pin_bit_mask = (   // Bit mask of the pins to configure
                            PIN_BM_A1 |
                            PIN_BM_A2 |
                            PIN_BM_B1 |
                            PIN_BM_B2
                        ), 
        .mode = GPIO_MODE_OUTPUT_OD,                  // Set as Output with Open Drain mode
        .pull_up_en = GPIO_PULLUP_ENABLE,             // Enable internal pull-up resistor 
        .pull_down_en = GPIO_PULLDOWN_DISABLE,        // Disable internal pull-down resistor
        .intr_type = GPIO_INTR_DISABLE                // Disable interrupts
    };

    // 2. Apply the configuration
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO configuration failed with error: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "GPIO configured.");

}



uint8_t coil_current_direction;
#define COIL_DIRECTION_OFF  0
#define COIL_DIRECTION_UP   1
#define COIL_DIRECTION_DOWN 2

void coil_set_direction(uint8_t coil_direction) {
    switch (coil_direction) {
        case 1:
        GPIO.out_w1tc = PIN_BM_A1 | PIN_BM_B1;
        GPIO.out_w1ts = PIN_BM_A2 | PIN_BM_B2;
        break;

        case 2:
        GPIO.out_w1tc = PIN_BM_A2 | PIN_BM_B2;
        GPIO.out_w1ts = PIN_BM_A1 | PIN_BM_B1;
        break;

        default:
        GPIO.out_w1tc = PIN_BM_A1 | PIN_BM_B1 | PIN_BM_A2 | PIN_BM_B2;
        break;
    }
    coil_current_direction = coil_direction;
}


#define COIL_OFF     0x00
#define COIL_TOP     0x01
#define COIL_BOTTOM  0x02
#define COIL_BOTH    0x03

uint8_t coil_current_duty;

void coil_set_duty(uint8_t coil, uint8_t duty)
{
    if (coil & COIL_TOP) {
        ledc_set_duty(COIL_MODE, COIL_TOP_CHANNEL, duty);
        ledc_update_duty(COIL_MODE, COIL_TOP_CHANNEL);
        coil_top_duty = duty;
    }

    if (coil & COIL_BOTTOM) {
        ledc_set_duty(COIL_MODE, COIL_BOTTOM_CHANNEL, duty);
        ledc_update_duty(COIL_MODE, COIL_BOTTOM_CHANNEL);
        coil_bottom_duty = duty;
    }

}


void coil_dynamic_set_duty(float travel, uint8_t target_duty) {
    //const float center = 1.6f;
    //const float fade_width = 0.6f;        // Cross-fade band
    const float max_boost_factor = 1.6f; // extra power boost at the midpoint

    if (travel >= 4) {
        coil_set_duty(COIL_BOTH, 0);
        return;
    }
    
    // float low = center - fade_width;  
    // float high = center + fade_width; 

    float low = 0.9;  
    float high = 1.9; 
    
    float top_weight = 0.0f;
    float bottom_weight = 0.0f;
    float transition_boost = 1.0f;


    
    if (travel <= low) {
        // Fully bottom coil, no boost needed
        top_weight = 0.0f;
        bottom_weight = 1.0f;
        transition_boost = 1.0f;
    } else if (travel >= high) {
        // Fully top coil, no boost needed
        top_weight = 1.0f;
        bottom_weight = 0.0f;
        transition_boost = 1.0f;
    } else {
        // 1. Normalized position within the cross-fade band (0.0 to 1.0)
        float t = (travel - low) / (high - low);
        
        top_weight = t;
        bottom_weight = 1.0f - t;
        
        // 2. Parabolic Boost Shape: 4 * t * (1 - t)
        // This equals 0 at edges (t=0, t=1) and peaks at exactly 1.0 in the center (t=0.5)
        float parabolic_shape = 4.0f * t * (1.0f - t);
        
        // Scale the boost: 1.0 (no boost) up to max_boost_factor
        transition_boost = 1.0f + (max_boost_factor - 1.0f) * parabolic_shape;
    }
    
    // 3. Apply weights and transition boost to the target duty
    float raw_top = (float)target_duty * top_weight * transition_boost;
    float raw_bottom = (float)target_duty * bottom_weight * transition_boost;
    
    // 4. Clamp to prevent uint8_t overflow if target_duty * boost exceeds 255
    if (raw_top > 255.0f) raw_top = 255.0f;
    if (raw_bottom > 255.0f) raw_bottom = 255.0f;
    
    // 5. Round safely and apply
    uint8_t top_duty = (uint8_t)(raw_top + 0.5f);
    uint8_t bottom_duty = (uint8_t)(raw_bottom + 0.5f);
    
    coil_set_duty(COIL_TOP, top_duty);
    coil_set_duty(COIL_BOTTOM, bottom_duty);
}


void adc_config(void) {
    // 1. Initialize ADC Unit
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = EXAMPLE_ADC_UNIT,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    // 2. Configure the ADC Channel
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = EXAMPLE_ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_4, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_5, &config));

}

float get_theta(float data1, float data2){


        #define HALL_SCALE 3.1
        #define HALL_BIAS 0.34
        theta = atan2f(data1-HALL_BASELINE, data2-HALL_BASELINE);
        z_hall = HALL_SCALE *  (theta - HALL_BIAS);

        return theta;
}

// void coil_offset_test(void) {
//     for (uint16_t duty_cycle = 0; duty_cycle <= 255; duty_cycle += 1) {
//         printf("%d", duty_cycle);

//         for (coil_current_active = 0; coil_current_active <= 3; coil_current_active++) {
//             float avg_data_up = 0;
//             float avg_data_down = 0;

//             coil_set_direction(COIL_DIRECTION_UP);
//             coil_set_duty(duty_cycle);
//             vTaskDelay(pdMS_TO_TICKS(1));
//             for (uint16_t k = 1; k <= 10; k++) {
//                 theta = get_theta((float)adc1_data, (float)adc2_data);
                
//                 avg_data_up += ((float)theta - avg_data_up) / k; 
                
//                 vTaskDelay(pdMS_TO_TICKS(1));
//             }
//             coil_set_direction(COIL_DIRECTION_DOWN);
//             vTaskDelay(pdMS_TO_TICKS(1));
//             for (uint16_t k = 1; k <= 10; k++) {
//                 theta = get_theta((float)adc1_data, (float)adc2_data);
                
//                 avg_data_down += ((float)theta - avg_data_down) / k; 
                
//                 vTaskDelay(pdMS_TO_TICKS(1));
//             }
//             coil_set_duty(0);

//             float up_val = calibration_table[0].raw_val - avg_data_up;
//             float down_val = calibration_table[0].raw_val - avg_data_down;
//             printf(", %.6f, %.6f", up_val, down_val);
//         }
//         printf("\r\n");
        
//         vTaskDelay(pdMS_TO_TICKS(10));
//     }
//     vTaskDelay(pdMS_TO_TICKS(5000));
// }

void auto_range(void) {
            static uint8_t bottom_high_spike_counter = 0;
        static uint8_t top_high_spike_counter = 0;
        const uint8_t NOISE_THRESHOLD_SAMPLES = 5; // Must persist for 5 samples


        

        if (theta > calibration_table[LUT_SIZE-1].raw_val) {
            top_high_spike_counter++;
            if (top_high_spike_counter >= NOISE_THRESHOLD_SAMPLES) {
                calibration_table[LUT_SIZE-1].raw_val = theta;
                top_high_spike_counter = 0;
            }
        } else {
            top_high_spike_counter = 0; // Reset if it was just a transient spike
        }

        if (theta < calibration_table[0].raw_val) {
            bottom_high_spike_counter++;
            if (bottom_high_spike_counter >= NOISE_THRESHOLD_SAMPLES) {
                calibration_table[0].raw_val = theta;
                bottom_high_spike_counter = 0;
            }
        } else {
            bottom_high_spike_counter = 0; // Reset if it was just a transient spike
        }
}

float coil_offset_comp(float raw_data, float offset_scale) {
        float offset_data;
        float max_duty = coil_top_duty > coil_bottom_duty ? coil_top_duty : coil_bottom_duty;

        switch (coil_current_direction) {
            case COIL_DIRECTION_UP:
                offset_data = raw_data - (offset_scale * (COIL_OFFSET_CONSTANT * max_duty));
                break;

            case COIL_DIRECTION_DOWN:
                offset_data = raw_data + (offset_scale * (COIL_OFFSET_CONSTANT * max_duty));
                break;
            
            default:
                offset_data = raw_data;
                break;
        }

        return offset_data;
}


// void test_force_thingy(void) {
//             static uint8_t i;
//         static uint8_t up_stroke;

//         if (up_stroke) {
//             if ((1 < travel_mm) && (travel_mm < 3.5) && (i != 3)) {
//                 i = 3;
//                 coil_set_direction(COIL_DIRECTION_UP);
//                 coil_set_duty(0);
//             }else if ((travel_mm > 3.5) && (i != 0)) {
//                 i = 0;
//                 up_stroke = 0;
//                 coil_set_direction(COIL_DIRECTION_OFF);
//                 coil_set_duty(0);
//             } 
//         } else {
//             if ((2.5 < travel_mm) && (travel_mm < 3) && (i != 1)) {
//                 i = 1;
//                 coil_set_direction(COIL_DIRECTION_UP);
//                 coil_set_duty(255);
//             } else if ((0.5 < travel_mm) && (travel_mm < 2.5) && (i != 2)) {
//                 i = 2;
//                 coil_set_direction(COIL_DIRECTION_DOWN);
//                 coil_set_duty(255);
//             } else if ((travel_mm < 0.5)  && (i != 0)) {
//                 i = 0;
//                 up_stroke = 1;
//                 coil_set_direction(COIL_DIRECTION_OFF);
//                 coil_set_duty(0);
//             } else if ((3.7 < travel_mm) && (i != 0)) {
//                 i = 0;
//                 coil_set_direction(COIL_DIRECTION_OFF);
//                 coil_set_duty(0);
//             }
//         }
// }


/**
 * Maps a raw Hall Effect sensor reading to millimeters using linear interpolation.
 * Handles boundary conditions and clamps values safely.
 */
float map_hall_with_lut(float current_raw) {
    // Check lower bound (if sensor goes below our minimum calibration point)
    if (current_raw <= calibration_table[0].raw_val) {
        return calibration_table[0].mm_val;
    }
    
    // Check upper bound (if sensor goes above our maximum calibration point)
    if (current_raw >= calibration_table[LUT_SIZE - 1].raw_val) {
        return calibration_table[LUT_SIZE - 1].mm_val;
    }
    
    // Iterate through table segments to find where current_raw sits
    for (int i = 0; i < LUT_SIZE - 1; i++) {
        float x0 = calibration_table[i].raw_val;
        float x1 = calibration_table[i+1].raw_val;
        
        if (current_raw >= x0 && current_raw <= x1) {
            float y0 = calibration_table[i].mm_val;
            float y1 = calibration_table[i+1].mm_val;
            
            // Prevent division by zero if two identical raw entries exist
            if (x1 == x0) return y0;
            
            // Linear Interpolation: y = y0 + ((x - x0) / (x1 - x0)) * (y1 - y0)
            float fraction = (current_raw - x0) / (x1 - x0);
            float position_mm = y0 + fraction * (y1 - y0);
            
            return position_mm;
        }
    }
    
    return 0.0f; // Fallback safe default
}


void teleplot(void) {
        // Output to USB Serial in Teleplot Format
        // printf(">offset_data:%.2f\n", offset_data);
        printf(">travel_mm_offset:%.2f\n", travel_mm_offset);
        printf(">coil_top_duty:%d\n", coil_top_duty);
        printf(">coil_bottom_duty:%d\n", coil_bottom_duty);
       // printf(">active_coils:%d\n", coil_current_active);
        //printf(">z_hall:%.2f\n", z_hall);
        //printf(">theta:%.2f\n", theta);
        //printf(">theta_median:%.2f\n", theta_median);
        // printf(">up_stroke:%d\n", up_stroke);
        //printf(">adc1_data:%d\n", adc1_data);
        //printf(">adc2_data:%d\n", adc2_data);
        //printf(">filtered_data:%.2f\n", filtered_data);
        //printf(">travel_mm:%.2f\n", travel_mm);
        printf(">travel_mm_filtered:%.2f\n", travel_mm_filtered);
        // printf(">top_val:%f\n", calibration_table[LUT_SIZE-1].raw_val);
        // printf(">bottom_val:%f\n", calibration_table[0].raw_val);
}


// Simple 3-point median filter
float median3(float a, float b, float c) {
    if ((a > b) != (a > c)) return a;
    else if ((b > a) != (b > c)) return b;
    else return c;
}

#define WINDOW_SIZE 5  //Must be an odd number (e.g., 3, 5, 7)

typedef struct {
    float buffer[WINDOW_SIZE];
    uint8_t head;
    uint8_t count;
} MedianFilter;

void median_init(MedianFilter *mf) {
    mf->head = 0;
    mf->count = 0;
    for (int i = 0; i < WINDOW_SIZE; i++) {
        mf->buffer[i] = 0.0f;
    }
}

float median_update(MedianFilter *mf, float new_val) {
    // 1. Insert new value into circular buffer
    mf->buffer[mf->head] = new_val;
    mf->head = (mf->head + 1) % WINDOW_SIZE;
    
    if (mf->count < WINDOW_SIZE) {
        mf->count++;
    }

    // 2. Copy active elements to a temporary array for sorting
    float temp[WINDOW_SIZE];
    for (uint8_t i = 0; i < mf->count; i++) {
        temp[i] = mf->buffer[i];
    }

    // 3. Insertion sort (extremely fast for small N <= 7)
    for (uint8_t i = 1; i < mf->count; i++) {
        float key = temp[i];
        int j = (int)i - 1;
        while (j >= 0 && temp[j] > key) {
            temp[j + 1] = temp[j];
            j--;
        }
        temp[j + 1] = key;
    }

    // 4. Return the median element
    return temp[mf->count / 2];
}

typedef struct {
    float pos;       // Filtered position estimate
    float velocity;  // Filtered velocity estimate (units per second)
    float alpha;     // Position tuning parameter (0 < alpha < 1)
    float beta;      // Velocity tuning parameter (0 < beta < 1)
} AlphaBetaFilter;

void alphabeta_init(AlphaBetaFilter *ab, float initial_pos, float alpha, float beta) {
    ab->pos = initial_pos;
    ab->velocity = 0.0f;
    ab->alpha = alpha;
    ab->beta = beta;
}

float alphabeta_update(AlphaBetaFilter *ab, float measurement, float dt) {
    if (dt <= 0.0f) return ab->pos; // Guard against zero/negative time steps

    // 1. PREDICT: Project the state forward based on current velocity
    float pos_pred = ab->pos + (ab->velocity * dt);
    float vel_pred = ab->velocity; // Velocity is assumed constant in the prediction step

    // 2. INNOVATION / RESIDUAL: Find the difference between actual measurement and prediction
    float residual = measurement - pos_pred;

    // 3. UPDATE: Correct position and velocity using alpha and beta gains
    ab->pos = pos_pred + (ab->alpha * residual);
    ab->velocity = vel_pred + ((ab->beta / dt) * residual);

    return ab->pos;
}


#define EX_UART_NUM UART_NUM_0
#define BUF_SIZE (1024)




void uart_init(void) {
    // 1. Configure UART parameters
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    // 2. Install the driver with an RX ring buffer
    // UART0 already uses GPIO1 (TX) and GPIO3 (RX) by default on the NodeMCU-32S
    ESP_ERROR_CHECK(uart_driver_install(EX_UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(EX_UART_NUM, &uart_config));
}

void uart_receive(void) {
    static uint8_t rx_buffer[BUF_SIZE + 1];

    // Read data directly into your fixed-size array
    // We pass (BUF_SIZE) to leave 1 byte safe for the null-terminator
    int len = uart_read_bytes(EX_UART_NUM, rx_buffer, BUF_SIZE, 0);
    
    if (len > 0) {
        rx_buffer[len] = '\0'; // Safely null-terminate the string
        printf("Received %d bytes: %s\n", len, (char *)rx_buffer);
        
    }
}

uint8_t uart_receive_byte(void) {
    static uint8_t out_byte;
    // Request strictly 1 byte so extra bytes stay safely in the UART ring buffer
    int len = uart_read_bytes(EX_UART_NUM, &out_byte, 1, 0);
    
    return out_byte;
}

void app_main(void) {

    uart_init();

    io_config();
    adc_config();
    coil_pwm_init();

    
    coil_set_direction(COIL_DIRECTION_UP);
    coil_set_duty(COIL_BOTH, 0);

    MedianFilter mf_travel;
    median_init(&mf_travel);

    // AlphaBetaFilter ab_travel;
    // alphabeta_init(&ab_travel, 0, 0.6, 0.1);

    while (1) {

        //coil_offset_test();

        // Read Raw Value
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_4, &adc1_data));
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_5, &adc2_data));
        


        theta = get_theta((float)adc1_data, (float)adc2_data);


        float offset_data = coil_offset_comp(theta, 1);

    
        travel_mm = map_hall_with_lut(theta);
        travel_mm_offset = map_hall_with_lut(offset_data);

        travel_mm_filtered = median_update(&mf_travel, travel_mm_offset);

        //travel_mm_filtered = alphabeta_update(&ab_travel, travel_mm_filtered, 1);

        uint8_t test_duty = uart_receive_byte();

        coil_dynamic_set_duty(travel_mm_filtered, test_duty);

        

        static uint8_t duty_cycle = 0;

        // coil_set_duty(duty_cycle);
        // duty_cycle++;
        // if (!duty_cycle) {
        //     switch (coil_current_direction)
        //     {
        //     case COIL_DIRECTION_UP:
        //         coil_set_direction(COIL_DIRECTION_DOWN);
        //         break;
            
        //     default:
        //         coil_set_direction(COIL_DIRECTION_UP);
        //         break;
        //     }
        // }

        //teleplot();

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

