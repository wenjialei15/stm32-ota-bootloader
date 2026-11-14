#include "test_support.h"

void run_crc_tests(void);
void run_image_tests(void);
void run_metadata_tests(void);
void run_boot_decision_tests(void);
void run_ota_proto_tests(void);

int main(void)
{
    printf("== STM32 OTA Bootloader · host tests ==\n\n");

    run_crc_tests();
    run_image_tests();
    run_metadata_tests();
    run_boot_decision_tests();
    run_ota_proto_tests();

    printf("\n--------------------------------------------\n");
    printf("%d checks, %d failures\n", g_checks, g_failures);

    return (g_failures == 0) ? 0 : 1;
}
