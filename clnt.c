/** 라즈베리파이 시뮬레이터 클라이언트 프로그램
 * 최종 작성 일시 : 2020-12-15
 * 
 * [프로그램 기능]
 * 라즈베리파이를 이용해 가습기와 현재 습도 상태를 간략하게 표시하는 클라이언트 프로그램
 * 
 * [프로그램 설명]
 * 서버에 일정 주기로 현재 온도/습도를 전송
 * 서버에서 판단하여 보낸 액션을 통해 클라이언트 동작
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>

#define BUFSIZE 200

#define RED "\x1b[31m" // RED, GREEN, BLUE 모두 LED용 printf 색상
#define GREEN "\x1b[32m"
#define BLUE "\x1b[34m"
#define YELLO "\x1b[33m" // 부저용 printf 색상

#define RED_LED 0
#define GREEN_LED 1
#define BLUE_LED 2

#define OFF 0
#define ON 1

#define RESET_COLOR "\x1b[0m" // 색상 초기화

void error_handling(char *message);
void end_write(int sock);
void ls_read(int sock);
void get_func(int sock, char *ftp_arg);
void put_func(int sock);

void sock_read(int sock);

bool red_led = false;
bool green_led = false;
bool blue_led = false;
bool humidifier = false;

int main(int argc, char *argv[])
{
    int sock;
    struct sockaddr_in serv_addr;

    int str_len = 0;
    char buf[BUFSIZE];

    if (argc != 3)
    {
        printf("Usage : <IP><port>\n");
        exit(1);
    }
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket error");
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("100 error : server connection error");

    sock_read(sock);

    while (true)
    {
        sleep(5); // 테스트를 위해 20초로 설정
        // sleep(10);
        srand(time(NULL));
        int temper = rand() % 21 + 10; // 온도 : 범위 10~30으로 제한
        int humid = rand() % 100; // 습도 : 범위 0~99로 제한
        
        sprintf(buf, "1.1.1.1 %d %d", temper, humid);
        printf("온도 : %d\n", temper);
        printf("습도: %d\n", humid);
        write(sock, buf, BUFSIZE);

        read(sock, buf, BUFSIZE); // 명령 코드 수신

        int humi_code;
        int led_code;
        int buz_code;

        humi_code = atoi(strtok(buf, "."));
        led_code = atoi(strtok(NULL, "."));
        buz_code = atof(strtok(NULL, "."));

        if(led_code == RED_LED)
        {
            if(red_led == false)
            {
                red_led = true;
                printf(RED "RED 전등이 켜졌습니다.\n" RESET_COLOR);
            }
            
            if(green_led == true)
            {
                green_led = false;
                printf(GREEN "green 전등이 꺼졌습니다.\n" RESET_COLOR);
            }
                
            if(blue_led == true)
            {
                blue_led = false;
                printf(BLUE "blue 전등이 꺼졌습니다.\n" RESET_COLOR);
            }
                

            if(humi_code == OFF && humidifier == true)
            {
                humidifier = false;
                printf("가습기를 종료합니다.\n");
            }
                
            else if(humi_code == ON && humidifier == false)
            {
                humidifier = true;
                printf("습도가 너무 낮습니다. 가습기를 동작합니다.\n");
            }
        }
        else if(led_code == GREEN_LED)
        {

            if(red_led == true)
            {
                red_led = false;
                printf(RED "RED 전등이 꺼졌습니다.\n" RESET_COLOR);
            }
            
            if(green_led == false)
            {
                green_led = true;
                printf(GREEN "green 전등이 켜졌습니다.\n" RESET_COLOR);
            }
                
            if(blue_led == true)
            {
                blue_led = false;
                printf(BLUE "blue 전등이 꺼졌습니다.\n" RESET_COLOR);
            }       
                
            else if(humi_code == ON && humidifier == false)
            {
                humidifier = true;
                printf("습도가 너무 낮습니다. 가습기를 동작합니다.\n");
            }
            
            if(humi_code == OFF && humidifier == true)
            {
                humidifier = false;
                printf("습도가 적정 상태입니다. 가습기를 종료합니다.\n");
            }
                
            
        }
        else if(led_code == BLUE_LED)
        {
            if(red_led == true)
            {
                red_led = false;
                printf(RED "RED 전등이 꺼졌습니다.\n" RESET_COLOR);
            }
            
            if(green_led == true)
            {
                green_led = false;
                printf(GREEN "green 전등이 꺼졌습니다.\n" RESET_COLOR);
            }
                
            if(blue_led == false)
            {
                blue_led = true;
                printf(BLUE "blue 전등이 켜졌습니다.\n" RESET_COLOR);
            }       
                
            else if(humi_code == ON && humidifier == false)
            {
                humidifier = true;
                printf("습도가 너무 낮습니다. 가습기를 동작합니다.\n");
            }
            
            if(humi_code == OFF && humidifier == true)
            {
                humidifier = false;
                printf("습도가 너무 높습니다!. 가습기를 종료합니다.\n");

                if(buz_code == ON)
                {
                    printf(RED "습도가 너무 높습니다! 환기를 시켜주세요!\n");
                    printf(RED "부저가 작동합니다!\n" RESET_COLOR);

                }
            }
        }
        printf("-------------------------------------------------------\n");

    }
    close(sock);
    return 0;
}

/** 에러 발생시 예외처리를 위한 함수
 * @param   message 예외처리시 출력할 에러 메시지가 담긴 문자열의 시작 주소
 */
void error_handling(char *message)
{
    char buf[BUFSIZE * 2];
    sprintf(buf, "%s", message);
    fputs(buf, stderr);
    fputc('\n', stderr);
    exit(1);
}

/** 서버에게 모든 메시지를 전송했을 때 호출하는 함수
 * @param   sock 통신이 끝났음을 전송할 클라이언트 소켓 번호
 */
void end_write(int sock)
{
    write(sock, "", 1);
}

/** 서버의 전송이 끝날때까지 서버로부터 메시지를 읽는 함수
 * @param   sock 메시지를 읽을 서버의 소켓 번호
 */
void sock_read(int sock)
{
    char buf[BUFSIZE];

    while (true)
    {
        read(sock, buf, BUFSIZE);

        if (!strcmp(buf, "")) // 서버가 전송이 끝났음을 의미
            break;
        printf("%s", buf);

        if (!strcmp(buf, "접속을 종료합니다.\n")) // 서버 접속자가 너무 많을 시
        {
            close(sock);
            exit(0);
        }
    }
}