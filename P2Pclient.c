#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <pthread.h>

#define BUFFER_SIZE 1024
#define MAXPEERS 3
#define MAX_WORDS_LENGTH 256
#define MAX_NUM_WORDS 1000
#define TIMER 30


// 0526 1503 edited
int broadcast_signal=0; //broadcast signal
int my_count=0; //내 검색어 개수

typedef struct // 단어 정보를 저장하는 구조체
{
    char word[MAX_WORDS_LENGTH];
    int count;
} WordInfo;

typedef struct // peer 구조체
{
  int port;
  char *ip;
  char *name;
  int fd;
} peer;


/*  -Shared peer information table
    -port,ip,name 상황에 맞춰서 변경
    -MAXPEERS 값에 따라 정보 추가 가능
 */
int my_index;

peer peer_info[MAXPEERS] = {
    {8001, "192.168.17.26", "Yechan",0},
    {8002, "192.168.17.143", "Jeongyeop",0},
    {8003, "192.168.17.82", "Mira",0}
    
};


int index_lookup(char *name); // name -> 주소 공유 테이블 내 index
void* packet_check(void); // 패킷 쿼리 추출 및 업데이트 관리하는 상위 함수
void append_to_file(const char* file_path, const char* new_string); // 파일에 문자열 추가하는 모듈
char* parse_query_param(char *query_value); // 쿼리 추출 모듈
void* broadcast (void); // 연결된 모든 동료들에 대해 파일을 전송하는 모듈
int search(void); // 파일에서 모든 단어를 읽고, 각 단어의 개수를 세어 정렬하여 출력하는 모듈
int compare(const void *a, const void *b); // search()내 qsort()에 필요한 비교 함수
void timer(); // 업데이트 타이머 모듈


int main(int argc, char **argv)
{
    /*
     쓰레드
     */
    
    pthread_t thread1;
    pthread_t thread2;
    
    int result1= pthread_create(&thread1,NULL,packet_check,NULL); // packet_check()실행 쓰레드1
    if (result1 != 0) {
        printf("Failed to create thread1\n");
        return 1;
    }
    
    int result2= pthread_create(&thread2,NULL,broadcast,NULL); // broadcast()실행 쓰레드2 : broadcast()
    if (result2 != 0) {
        printf("Failed to create thread2\n");
        return 1;
    }
    
    
    if (argc < 2)
    {
        printf("\033[3;33m\nProvide the name of the peer.\033[0m\n");
        exit(EXIT_FAILURE);
    }
    
    
    my_index = index_lookup(argv[1]); // 접속자의 index
    if (my_index == -1) // 접속하고자 하는 사람이 주소정보 테이블에 등록되어 있지 않다면
    {
        printf("\033[3;33m\n%s is not in the peers group. This incident will be reported.\033[0m\n", argv[1]);
        exit(EXIT_FAILURE);
    }
    
    
    
    int server_fd;
    fd_set read_fds;
    int max_fd, activity, i, valread, new_socket;
    struct sockaddr_in server_addr, client_addr;
    char buffer[BUFFER_SIZE];
    
    // 서버 소켓 생성
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    // 서버 주소 설정
    bzero(&server_addr, sizeof(server_addr)); // 접속 계정의 주소 메모리 0으로 초기화
    server_addr.sin_family = AF_INET; // ipv4
    server_addr.sin_addr.s_addr = inet_addr(peer_info[my_index].ip); // 공유테이블 속 접속 계정의 ip 세팅
    server_addr.sin_port = htons(peer_info[my_index].port); // 공유 테이블 속 접속 계정의 포트 세팅
    
    
    // 소켓과 주소를 바인딩
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    
    // 연결 수신 대기열 설정
    if (listen(server_fd, MAXPEERS) == -1) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }
    
    
    printf("\n\033[0;35m++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\033[0m\n");
    printf("\033[0;32m안녕하세요 \033[0m\033[3;32m%s\033[0m\033[0;32m님! P2P 기반 실시간 검색어 공유 시스템에 오신걸 환영합니다.\n브라우저를 통해 검색하면 검색어가 등록된 모든 PEER들에게 공유 됩니다!\033[1;32m\n", argv[1]);
    printf("\033[0;35m++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\033[0m\n\n");
    
    
    while(1){ // 무한 루프
        
            // 파일 디스크립터 집합 초기화
            FD_ZERO(&read_fds);
            FD_SET(server_fd, &read_fds);
            max_fd = server_fd;
            
            
            // 연결된 클라이언트 소켓들을 파일 디스크립터 집합에 추가
            for (i = 0; i < MAXPEERS; i++) {
                if(i != my_index){
                int client_fd = peer_info[i].fd;
                    if (client_fd > 0) {
                        FD_SET(client_fd, &read_fds);
                        if (client_fd > max_fd) {
                            max_fd = client_fd;
                        }
                    }
                }
            }
            
            
            // 상태 변화 감지
            activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
            if (activity == -1) {
                perror("select failed");
                exit(EXIT_FAILURE);
            }
            
            
            // 새로운 클라이언트 연결 처리
            if (FD_ISSET(server_fd, &read_fds)) {
                int addrlen = sizeof(client_addr);
                if ((new_socket = accept(server_fd, (struct sockaddr*)&client_addr, (socklen_t*)&addrlen)) == -1) {
                    perror("accept failed");
                    exit(EXIT_FAILURE);
                }
                
                
                // 연결된 클라이언트 소켓을 배열에 추가
                for (i = 0; i < MAXPEERS; i++) {
                    if(i != my_index){
                        if (peer_info[i].fd == 0) {
                            peer_info[i].fd = new_socket;
                            break;
                        }
                    }
                }
            }
            
            
            // 클라이언트로부터의 데이터 수신 및 처리
            for (i = 0; i < MAXPEERS; i++) {
                if(i != my_index){
                    int client_fd = peer_info[i].fd;
                    if (FD_ISSET(client_fd, &read_fds)) {
                        if ((valread = read(client_fd, buffer, BUFFER_SIZE)) == 0) {
                            // 클라이언트가 연결을 종료한 경우
                            close(client_fd);
                            peer_info[i].fd = 0;
                        } else {
                            // 데이터를 기존 파일에 덮어쓰기
                            FILE *file = fopen("test.txt", "w");
                            if (file == NULL) {
                                perror("fopen failed");
                                exit(EXIT_FAILURE);
                            }
                            fwrite(buffer, 1, valread, file);
                            fclose(file);
                        }
                    }
                }
            }
        } // while
        
    return 0;
}
        
        
        
int index_lookup(char *name) // name -> 주소 공유 테이블 내 index
{
  for (int i = 0; i < MAXPEERS; i++)
    if (strcmp(peer_info[i].name, name) == 0)
      return i;

  return -1; // 존재하지 않으면 index = -1
}


void* packet_check(void) {  // // 패킷 쿼리 추출 및 업데이트 관리하는 상위 함수
    FILE *fp;
    char buffer[256];
    char str[256];
    char command[] = "tshark -r example.pcap -Y http.request.method==GET -O http"; //커맨드 명령어 저장
    int semi_signal=0;

    while (1) {
        int count = 0;//현재 http 패킷 쿼리 개수
        fp = popen(command, "r"); // 커맨드를 실행하고 그 결과를 읽기 모드로 열린 파일스트림을 반환
        
        if (fp == NULL) {
            printf("Error executing tshark\n");
            return 0;
        }
        
        FILE* file = fopen("./test.txt", "r");
        if (file == NULL) {
            printf("file can not open\n");
            return 0;
        }
        fclose(file); // 단순히 파일 읽기 모드로 열리는 지 체크 코드이기 때문에
        
        while (fgets(buffer, sizeof(buffer), fp) != NULL) { // 명령줄 결과 한 줄씩 읽어와 buffer에 저장 , EOF에 도달할 떄까지 반복
            if (strstr(buffer, "Referer: ") != NULL) {  //읽어온 줄에 Referer: 문자열이 포함된 줄인지 확인
                char *referer = strstr(buffer, "Referer: "); // 포함되어 있다면 위치 저장
                
                if (referer != NULL) {
                    referer += 9; //"Referer:"이후를 가리키게 위치 변경
                    
                    char *rn_ptr = strstr(referer, "\\r\\n"); //"\\r\\n"문자열 찾음
                    if (rn_ptr != NULL) {
                        *rn_ptr = '\0'; //\r\n을 NULL문자로 대체하여 문자열을 끊음
                    }
                    
                    char* temp = parse_query_param(referer); //한줄에 대해 뽑아낸 쿼리를 temp에 저장
                    
                    if(temp){ // 추출 성공 시
                        count++; // 추출한 쿼리 개수 카운트
                        
                        if(count <= my_count){
                            continue;//이전까지 추출한 쿼리 개수보다 작거나 같으면 무시
                        }
                        else if(count > my_count){ // 업데이트가 된것임. 이전까지 추출한 쿼리 개수보다 높은 쿼리새로운 쿼리 파일에 추가
                            append_to_file("./test.txt", temp);
                            my_count=count;
                            semi_signal=1; // 업데이트-브로드캐스트 전조 신호
                        }
                    } // if(temp)
                } // if (referer != NULL) {
            } //  if (strstr(buffer, "Referer: ") != NULL)
        } // while (fgets(buffer, sizeof(buffer), fp) != NULL
        
        
    if(semi_signal==1){ //새로운 패킷 추출에 성공했으면(파일이 업데이트 됬다면)
        broadcast_signal=1; //브로드캐스트 trigger ON
        semi_signal=0; // 추출 신호 OFF
    }
    else{
        printf("\033[0;35m--------------------------------------------------------------------------\033[0m\n");
        printf("업데이트를 위해 포털사이트에서 검색을 해보세요!\n");
        printf("\033[0;35m--------------------------------------------------------------------------\033[0m\n\n");
    }
    
    sleep(TIMER);//60초 주기
    timer(); //
    
  //  menu();
        pclose(fp);
        
   } // while(1)
 
}


void append_to_file(const char* file_path, const char* new_string) { // test.txt파일에 추가된 쿼리 추가하는 모듈
    if(new_string){
            // 파일을 이어쓰기 모드로 열기
            FILE* file = fopen(file_path, "a");
            if (file == NULL) {
                printf("file can not open\n");
                return;
            }
            // 새로운 문자열을 파일에 추가
            fprintf(file, "%s\n", new_string); // test.txt파일에 이어쓰기
        
            // 파일 닫기
            fclose(file);
    }
}

    
char* parse_query_param(char *query_value) {//쿼리 추출 모듈
    
    char *query_param = NULL;
    char *query_token = strtok(query_value, "&"); // &를 기준으로 토큰화 -> 첫번째 토큰 위치 반환
    
    while (query_token != NULL) { // 마지막 토큰 까지
        if (strstr(query_token, "q") != NULL) { //query_token에 "query=" 문자열이 포함되어 있는지 확인
            query_param = strtok(query_token, "=");//또다시 = 문자로 기준으로 토큰화하여 첫번째 토큰을얻음
            query_param = strtok(NULL, "=");// =를 기준으로 한 다음 토큰으로 이동
            break;
        }
        
        query_token = strtok(NULL, "&");// &를 기준으로 한 다음 토큰으로 이동
    }
    
    if (query_param != NULL) {
        char *rn_ptr = strstr(query_param, "\\r\\n");// query_param에서 "\\r\\n" 문자열을 찾음
        if (rn_ptr != NULL) {
            *rn_ptr = '\0'; //"\\r\\n문자열을 NULL문자로 대체하여 문자열을 자름
        }
        
        return query_param; //query=로 시작하는 파라미터를 추출하여 반환 , 찾지 못한 경우 NULL 반환
    }
} // end

void* broadcast (void) //연결된 모든 동료들에 대해 파일을 전송하는 모듈
{
    
    while(1){
        if(broadcast_signal==1){ //브로드 캐스트 신호가 켜지면 시작 // 기본값은 0
          
            int client_fd;
            struct sockaddr_in server_addr;
            char buffer[BUFFER_SIZE];
            int opt;
            
           
         
            for (int i = 0; i < MAXPEERS; i++) {
                if(i != my_index){
                    // 클라이언트 소켓 생성
                    client_fd = socket(AF_INET, SOCK_STREAM, 0);
                    setsockopt(client_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
                    
                    if (client_fd == -1) {
                        perror("socket failed");
                        exit(EXIT_FAILURE);
                    }
                    server_addr.sin_family = AF_INET;
                    server_addr.sin_port = htons(peer_info[i].port);
                    if (inet_pton(AF_INET, peer_info[i].ip, &(server_addr.sin_addr)) <= 0) {
                        perror("inet_pton failed");
                        exit(EXIT_FAILURE);
                    }
                    
                    if (connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
                        perror("connect failed");
                        exit(EXIT_FAILURE);
                    }
                    printf("\033[0;35m--------------------------------------------------------------------------\033[0m\n");
                    printf("PEER: %s와 소켓 %d를 통해 연결이 완료되었습니다!\n",peer_info[i].name,client_fd);
                    printf("%s에게 전송중...\n\n",peer_info[i].name);
                    // 전송할 파일 열기
                    FILE* file = fopen("test.txt", "r");
                    if (file == NULL) {
                        perror("fopen failed");
                        exit(EXIT_FAILURE);
                    }
                    
                    // 파일 데이터를 서버로 전송
                    ssize_t bytesRead;
                    int check;
                    while ((bytesRead = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
                        check = write(client_fd, buffer, bytesRead);
                        if (check == -1) {
                            perror("write failed");
                            exit(EXIT_FAILURE);
                        }
                        else{
                            printf("PEER: %s에게 검색어 공유 성공!(%d byte)\n",peer_info[i].name, check);
                            printf("\033[0;35m--------------------------------------------------------------------------\033[0m\n\n");
                        }
                    }
                    // 파일 전송 완료 후 클라이언트 소켓 종료
                    fclose(file);
                    close(client_fd);
                }
            } //for
            broadcast_signal = 0;
        } // if(broadcast_signal==1){
    } // while(1)
} // end

// 단어를 개수별로 정렬하는 비교 함수

int search(void) // 파일에서 단어를 읽고, 각 단어의 개수를 세어 정렬하여 출력하는 모듈
{
    FILE *file5;
    char buffer[MAX_WORDS_LENGTH]; // 쿼리 최대 길이
    WordInfo words[MAX_NUM_WORDS]; // 쿼리 최대 개수 구조체 배열
    int numWords = 0; // 단어 개수

    // 파일 열기
    file5 = fopen("test.txt", "r");
    if (file5 == NULL)
    {
        perror("Error opening file");
        exit(1);
    }

    // 각 단어의 개수 세기
    while (fgets(buffer, sizeof(buffer), file5) != NULL) // 파일에서 한 줄씩 buffer로 읽어옴
    {
        // 개행 문자 제거
        if (buffer[strlen(buffer) - 1] == '\n')
        {
            buffer[strlen(buffer) - 1] = '\0';
        }

        // 단어 검색
        int found = 0;
        for (int i = 0; i < numWords; i++)
        {
            if (strcmp(buffer, words[i].word) == 0)
            {
                words[i].count++;
                found = 1;
                break;
            }
        }
        // 새로운 단어인 경우 추가
        if (!found)
        {
            strncpy(words[numWords].word, buffer, sizeof(words[numWords].word)); // 구조체 배열에 추가
            words[numWords].count = 1; // 특정 단어의 검색 횟수
            numWords++; // 구조체 배열에 있는 단어 개수
        }
    } //  while (fgets(buffer, sizeof(buffer), file5) != NULL)

    // 문장을 개수별로 정렬 - qsort
    qsort(words, numWords, sizeof(WordInfo), compare);
    
    // 정렬된 결과 출력
    printf("\033[0;35m--------------------------------------------------------------------------\033[0m\n");
    printf("<실 시 간   검 색 어   목 록>\n");
    for (int i = 0; i < numWords; i++)
    {
        prinf("Word: %s, Count: %d\n", words[i].word, words[i].count);
    }
    printf("\033[0;35m--------------------------------------------------------------------------\033[0m\n\n");

    // 파일 닫기
    fclose(file5);

    return 0;
}


int compare(const void *a, const void *b) // search()내 qsort()에 필요한 비교 함수
{
    WordInfo *wordA = (WordInfo *)a;
    WordInfo *wordB = (WordInfo *)b;
    return wordB->count - wordA->count;
}

void timer()
{
    int remaining_time = TIMER;

    while (remaining_time > 0)
    {
        printf("\033[0;35m--------------------------------------------------------------------------\033[0m\n");
        printf("공지) %d초 후에 실시간 검색어 update 예정입니다!\n", remaining_time);
        sleep(15); // 1초 대기
        remaining_time -= 15;
    }
    printf("\033[0;35m--------------------------------------------------------------------------\033[0m\n\n");
    
    printf("\033[0;35m--------------------------------------------------------------------------\033[0m\n");
    printf("공지) 최신의 실시간 검색어 업데이트를 시작합니다. 잠시만 기다려주세요!\n");
    printf("\033[0;35m--------------------------------------------------------------------------\033[0m\n\n");
    search();
}
