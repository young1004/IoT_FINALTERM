/** 라즈베리파이 시뮬레이터와의 통신을 하기 위한 서버 프로그램
 * 최종 작성 일시 : 2020-12-15
 * 
 * [프로그램 기능]
 * 가상 라즈베리파이 시뮬레이터로부터 전송받은 값을 수신
 * 수신한 데이터를 통해 가습기 조절 및 현재 습도를 LED 센서를 이용하여 표현
 * 
 * [프로그램 설명]
 * 시뮬레이터에서 수신한 온도/습도 데이터를 통해 가습기를 조절
 * 온도별 적정 습도를 통해 가습기를 on/off
 * 현재 습도가 낮은지, 높은지, 적정한지를 LED를 통해 표현
 * - RED : 습도 낮음 / Green : 습도 적정 / BLUE : 습도 높음
 * 일정 주기마다 각 클라이언트에게 온도/습도를 제공받음
 * 
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

#define BUFSIZE 200
#define MAXUSERS 10
#define IPSIZE 20

#define FCODE1 "60주년 기념관"
#define FCODE2 "집"

#define SCODE0 ""
#define SCODE1 "1층"
#define SCODE2 "2층"
#define SCODE3 "3층"

#define TCODE0 ""
#define TCODE1 "1호"
#define TCODE2 "2호"
#define TCODE3 "3호"

#define SENSOR1 "온&습도센서"
#define SENSOR2 "LED센서"
#define SENSOR3 "부저센서"

#define LGDIR "/home/mylinux/IoTFinal/logs"

void write_act(int sock, int temper, int humid);
void *clnt_connection(void *arg);
void error_handling(char *message);

void end_write(int sock);

int get_mutx_no(char *filename);
void get_ipaddr(int sock, char *buf);
void get_now_time(struct tm *nt);

void write_log(char *message, char *logdir, bool flag);

int clnt_number = 0;
int clnt_socks[MAXUSERS];

int list_number = 0;
char mutx_lists[MAXUSERS][BUFSIZE];

pthread_mutex_t mutx;
pthread_mutex_t file_mutex[MAXUSERS];

int main(int argc, char **argv)
{
    int serv_sock, clnt_sock;
    struct sockaddr_in serv_addr;
    struct sockaddr_in clnt_addr;
    int clnt_addr_size;
    pthread_t thread;

    if (argc != 2)
    {
        printf("Usage : %s <port>\n", argv[0]);
        exit(1);
    }
    if (pthread_mutex_init(&mutx, NULL))
        error_handling("mutex init error");
    for (int i = 0; i < MAXUSERS; i++)
        if (pthread_mutex_init(&file_mutex[i], NULL))
            error_handling("mutex init error");

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));
    if (bind(serv_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("bind() error");
    if (listen(serv_sock, 5) == -1)
        error_handling("listen() error");

    while (1)
    {
        clnt_addr_size = sizeof(clnt_addr);
        clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_addr, &clnt_addr_size);
        if (clnt_number >= MAXUSERS)
        {
            write(clnt_sock, "접속자가 너무 많습니다.\n", BUFSIZE);
            write(clnt_sock, "접속을 종료합니다.\n", BUFSIZE);
            end_write(clnt_sock);
            close(clnt_sock);
        }
        else
        {
            write(clnt_sock, "FTP 서버에 접속되었습니다.\n", BUFSIZE);
            end_write(clnt_sock);

            pthread_mutex_lock(&mutx);
            clnt_socks[clnt_number++] = clnt_sock;
            pthread_mutex_unlock(&mutx);
            pthread_create(&thread, NULL, clnt_connection, (void *)&clnt_sock);
            printf("new client access. client IP : %s \n", inet_ntoa(clnt_addr.sin_addr));
        }
    }
    return 0;
}

/** 클라이언트 접속시, 생성된 스레드가 동작할 기준이 되는 함수
 * @param   arg 생성된 스레드가 맡을 클라이언트 소켓 번호
 * @return  해당 스레드가 정상 종료되었는지에 대한 상태 코드
 */
void *clnt_connection(void *arg)
{
    // 클라이언트 별 스레드 동작을 위한 변수들
    int clnt_sock = *(int *)arg;
    char buf[BUFSIZE];
    int str_len = 0;
    FILE *fp;

    char mib[BUFSIZE]; // 클라이언트 MIB값
    char mib_place[BUFSIZE]; // 클라이언트 MIB에 따른 장소를 저장할 변수
    int temper; // 온도값
    int humid; // 습도값

    //table 작성을 위한 변수
    char table_format[400];

    // log 파일 작성을 위한 변수들
    char clnt_ip[IPSIZE] = {0};
    char log_dir[BUFSIZE];
    char log_msg[400];

    sprintf(log_dir, LGDIR);
    get_ipaddr(clnt_sock, clnt_ip);
    // printf("%s\n", clnt_ip);

    sprintf(log_msg, "client [%d] access. client ip : %s\n", clnt_sock, clnt_ip);
    write_log(log_msg, log_dir, true);

    // 클라이언트 입력을 받는 부분!
    while ((str_len = read(clnt_sock, buf, BUFSIZE)) != 0)
    {
        bool MIB_flag = false;

        char *tmp = strtok(buf, " "); // MIB 추출
        strcpy(mib, tmp);

        tmp = strtok(NULL, " "); // 온도 추출
        temper = atoi(tmp);
        // strcpy(temper, atoi(tmp));

        tmp = strtok(NULL, " "); // 습도 추출
        humid = atoi(tmp);
        // strcpy(&humid, atoi(tmp));

        pthread_mutex_lock(&mutx);
        fp = fopen("MIB_TABLE.txt", "r");
        
        while (true)
        {
            
            strcpy(buf, "");
            fscanf(fp, "%[^\t]\t", buf); // mib값

            if (!strcmp(buf, ""))
                break;

            if(!strcmp(buf, mib))
            {
                MIB_flag = true;
                fscanf(fp, "%[^\t]\t", buf); // ip주소(이미 가지고 있음)
                fscanf(fp, "%[^\n]\n", buf); // 장소값
                strcpy(mib_place, buf);

                break;
            }
            else
            {
                fscanf(fp, "%[^\t]\t", buf);
                fscanf(fp, "%[^\n]\n", buf);
            }
        }
        fclose(fp);
        pthread_mutex_unlock(&mutx);
        
        if(MIB_flag) // DNS테이블에 존재하는 경우
        {
            write_act(clnt_sock, temper, humid);
            sprintf(log_msg, "client ip: [%s], client info : [%s]\n", clnt_ip, mib_place);
            write_log(log_msg, log_dir, true);
        }
        else // DNS 테이블에 존재하지 않는 경우
        {
            char place[BUFSIZE / 4];
            char place_1[BUFSIZE / 5];
            char place_2[BUFSIZE / 5];
            char sensor_no[BUFSIZE / 5];

            char place_full[BUFSIZE];

            pthread_mutex_lock(&mutx);
            fp = fopen("MIB_TABLE.txt", "a");
            strcpy(buf, mib); //MIB값을 buf 변수에 임시로 저장

            tmp = strtok(buf, "."); // 장소
            strcpy(place, tmp);

            tmp = strtok(NULL, "."); // 층수 (x층)
            strcpy(place_1, tmp);

            tmp = strtok(NULL, "."); // 호수 (xx호)
            strcpy(place_2, tmp);

            tmp = strtok(NULL, "."); // 센서 번호
            strcpy(sensor_no, tmp);
            
            if (!strcmp(place, "1"))
                sprintf(place_full, "%s ", FCODE1);
            else if(!strcmp(place, "2"))
                sprintf(place_full, "%s ", FCODE2);
            
            if (!strcmp(place_1, "0"))
                sprintf(place_full, "%s", place_full);
            else if(!strcmp(place_1, "1"))
                sprintf(place_full, "%s%s ", place_full, SCODE1);
            else if(!strcmp(place_1, "2"))
                sprintf(place_full, "%s%s ", place_full, SCODE2);
            else if(!strcmp(place_1, "3"))
                sprintf(place_full, "%s%s ", place_full, SCODE3);

            if (!strcmp(place_2, "0"))
                sprintf(place_full, "%s", place_full);
            else if(!strcmp(place_2, "1"))
                sprintf(place_full, "%s%s ", place_full, TCODE1);
            else if(!strcmp(place_2, "2"))
                sprintf(place_full, "%s%s ", place_full, TCODE2);
            else if(!strcmp(place_2, "3"))
                sprintf(place_full, "%s%s ", place_full, TCODE3);

            if (!strcmp(sensor_no, "1"))
                sprintf(place_full, "%s%s", place_full, SENSOR1);
            else if(!strcmp(sensor_no, "2"))
                sprintf(place_full, "%s%s", place_full, SENSOR2);
            else if(!strcmp(sensor_no, "3"))
                sprintf(place_full, "%s%s", place_full, SENSOR3);
            
            sprintf(table_format, "%s\t%s\t%s\n", mib, clnt_ip, place_full);
            fprintf(fp, "%s", table_format);

            fclose(fp);
            pthread_mutex_unlock(&mutx);

            write_act(clnt_sock, temper, humid);
            sprintf(log_msg, "client ip: [%s], client info : [%s]\n", clnt_ip, place_full);
            write_log(log_msg, log_dir, true);
        }
        
    }

    pthread_mutex_lock(&mutx);
    for (int i = 0; i < clnt_number; i++)
    { /* 클라이언트 연결 종료 시 */
        sprintf(log_msg, "client [%d] disconnected , ip : %s\n", clnt_sock, clnt_ip);
        write_log(log_msg, log_dir, false);
        if (clnt_sock == clnt_socks[i])
        {
            for (; i < clnt_number - 1; i++)
                clnt_socks[i] = clnt_socks[i + 1];

            break;
        }
    }
    clnt_number--;
    pthread_mutex_unlock(&mutx);
    close(clnt_sock);
    return 0;
}

/** 클라이언트에게 동작 코드를 전송하는 함수
 * @param   sock 동작 코드를 전송할 클라이언트 소켓 번호
 * @param   temper 동작 코드 생성의 기준이 되는 온도 정보를 담은 변수
 * @param   humid 동작 코드 생성의 기준이 되는 습도 정보를 담은 변수
 */
void write_act(int sock, int temper, int humid)
{
    char act_code[BUFSIZE];

    if(temper<18)
    {
        if(humid<70)
            strcpy(act_code, "1.0.0");
        else if (humid == 70)
            strcpy(act_code, "0.1.0");
        else if (humid > 70)
            strcpy(act_code, "0.2.1");
    }
    else if(temper>=18 && temper <=20)
    {
        if(humid<60)
            strcpy(act_code, "1.0.0");
        else if (humid == 60)
            strcpy(act_code, "0.1.0");
        else if (humid > 60)
            strcpy(act_code, "0.2.1");
    }
    else if(temper >=21 && temper <=23)
    {
        if(humid<50)
            strcpy(act_code, "1.0.0");
        else if (humid == 50)
            strcpy(act_code, "0.1.0");
        else if (humid > 50)
            strcpy(act_code, "0.2.1");
    }
    else if(temper>=24)
    {
        if(humid<40)
            strcpy(act_code, "1.0.0");
        else if (humid == 40)
            strcpy(act_code, "0.1.0");
        else if (humid > 40)
            strcpy(act_code, "0.2.1");
    }

    write(sock, act_code, BUFSIZE);
}

/** 클라이언트에게 모든 메시지를 전송했을 때 호출하는 함수
 * @param   sock 통신이 끝났음을 전송할 클라이언트 소켓 번호
 */
void end_write(int sock)
{
    write(sock, "", 1);
}

/** 메시지를 받아 서버의 로그를 기록하는 함수
 * @param message 기록할 로그 메시지 문자열
 * @param logdir  로그파일이 기록될 디렉토리 경로
 * @param flag    mutex lock을 사용할것인지 아닌지에 대한 flag 변수
 */
void write_log(char *message, char *logdir, bool flag)
{
    FILE *log_file;
    char log_dir_name[BUFSIZE * 2];
    char log_file_name[BUFSIZE * 4];
    struct tm *log_time = (struct tm *)malloc(sizeof(struct tm));

    get_now_time(log_time);

    sprintf(log_dir_name, "%s/%d년 %d월", logdir, log_time->tm_year, log_time->tm_mon);
    mkdir(log_dir_name, 0755);

    sprintf(log_file_name, "%s/%d월 %d일.log", log_dir_name, log_time->tm_mon, log_time->tm_mday);

    if (flag)
        pthread_mutex_lock(&mutx);

    log_file = fopen(log_file_name, "a");

    fprintf(log_file, "[%d.%d.%d %02d:%02d:%02d] %s", log_time->tm_year, log_time->tm_mon, log_time->tm_mday, log_time->tm_hour, log_time->tm_min, log_time->tm_sec, message);
    fclose(log_file);

    if (flag)
        pthread_mutex_unlock(&mutx);
}

/** 입력받은 소켓의 ip 주소를 문자열에 저장해주는 함수
 * @param   sock ip 주소를 알고싶은 소켓 번호
 * @param   buf ip 주소를 저장할 문자 배열의 시작 주소
 */
void get_ipaddr(int sock, char *buf)
{
    struct sockaddr_in sockAddr;
    int size;

    size = sizeof(sockAddr);
    memset(&sockAddr, 0, size);
    getpeername(sock, (struct sockaddr *)&sockAddr, &size);

    strcpy(buf, inet_ntoa(sockAddr.sin_addr));
}

/** mutex lock을 걸 mutex 배열의 인덱스를 반환하는 함수
 * @param   filename mutex lock으로 잠굴 파일의 이름
 * @return  mutex lock을 사용할 mutex 배열 인덱스값
 */
int get_mutx_no(char *filename)
{
    for (int i = 0; i < list_number; i++)
    {
        if (!strcmp(mutx_lists[i], filename))
            return i;
    }
}

/** 연결 시간에 대한 정보를 리턴하는 함수
 * @param nt : 시간을 기록하기위해 받아올 struct tm 구조체의 주소
 */
void get_now_time(struct tm *nt)
{
    time_t now_time;
    struct tm *t;

    time(&now_time);
    t = (struct tm *)localtime(&now_time);

    nt->tm_year = t->tm_year + 1900;
    nt->tm_mon = t->tm_mon + 1;
    nt->tm_mday = t->tm_mday;
    nt->tm_hour = t->tm_hour;
    nt->tm_min = t->tm_min;
    nt->tm_sec = t->tm_sec;
}

/** 에러 발생시 예외처리를 위한 함수
 * @param   message 예외처리시 출력할 에러 메시지가 담긴 문자열의 시작 주소
 */
void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}