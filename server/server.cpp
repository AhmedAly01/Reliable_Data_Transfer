#include <utility>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <string>
#include <thread>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <chrono>
#include <bits/stdc++.h>

using namespace std;

#define maxSegSize 508
#define AckPacketSize 8
#define ChunkSize 499

enum FSMState
{
    fastRecovery,
    slowStart,
    congestionAvoidance
};

struct packet
{
    uint16_t checkSum;
    uint16_t len;
    uint32_t seqNo;
    char data[500];
};

struct packetNotSent
{
    int seqNo;
    bool isFinished;
    chrono::time_point<chrono::system_clock> timer;
};

struct packetAck
{
    uint16_t checkSum;
    uint16_t len;
    uint32_t ackno;
};

int port, randomSeed;
double plp;
vector<packetNotSent> packetNotSents;
vector<packet> packetsSent;
int Base_packet_number = 0;
long SentBytes = 0;
int Sst = 128;
bool Flag = true;
int SeqNum = 0;
long SentPacketsNotAcked = 0;
FSMState St = slowStart;
long NumberOfDupAcks = 0;
int LastAckedSeqNum = -1;
bool StillExistAcks = true;
char Rec_buf[maxSegSize];
socklen_t ClientAddressLength = sizeof(struct sockaddr);
int AlreadySentPackets = 0;

void handle_client_request(int serverSocket, int client_fd, sockaddr_in client_addr, char rec_buffer[], int bufferSize);
void sendTheData_HandleCongesion(int client_fd, struct sockaddr_in client_addr, vector<string> data);
bool send_packet(int client_fd, struct sockaddr_in client_addr, string temp_packet_string, int seqNum);

/*
  Read file of COntent of the file
*/
vector<string> readFileData(string fName)
{
    string temp = "";
    vector<string> dataPackets;
    ifstream ifStream;
    ifStream.open(fName);
    if (ifStream)
    {
        char c;
        int idx = 0;
        while (ifStream.get(c))
        {
            if (idx < ChunkSize)
            {
                temp += c;
            }
            else
            {
                dataPackets.push_back(temp);
                temp.clear();
                temp += c;
                idx = 0;
                continue;
            }
            idx++;
        }
        if (idx > 0)
        {
            dataPackets.push_back(temp);
        }
    }
    ifStream.close();
    return dataPackets;
}

/*
get Ack of check Sum
*/
uint16_t getAckChecksum(uint16_t len, uint32_t ackNo)
{
    uint32_t sum = 0;
    sum += len;
    sum += ackNo;
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    uint16_t Sum = (uint16_t)(~sum);
    return Sum;
}

/*
Get Data of Check Sum ;
*/
uint16_t getDataChecksum(string content, uint16_t len, uint32_t seqNo)
{
    uint32_t sum = 0;
    sum += len;
    sum += seqNo;
    char a[content.length() + 1];
    strcpy(a, content.c_str());
    for (int i = 0; i < content.length(); i++)
    {
        sum += a[i];
    }
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

/*
 create packet of file
*/
packet createPacket(string packetStr, int seqNo)
{
    struct packet p;
    memset(p.data, 0, 500);
    strcpy(p.data, packetStr.c_str());
    p.seqNo = seqNo;
    p.len = packetStr.size();
    p.checkSum = getDataChecksum(packetStr, p.len, p.seqNo);
    return p;
}

/*
check if data is Corrupted
*/
bool corruptDatagram()
{
    double isLost = (rand() % 100) * plp;
    if (isLost >= 5.9)
    {
        cout << "-------- Packet Loss Simulated --------" << isLost << endl;
        return true;
    }
    return false;
}

/*
   read commands inside of info.txt
*/
vector<string> readInfo()
{
    string fName = "info.txt";
    vector<string> reqs;
    string line;
    ifstream f;
    f.open(fName);
    while (getline(f, line))
    {
        reqs.push_back(line);
    }
    return reqs;
}

/*
    check if the file is exist or not
*/
long checkFileExistence(string fName)
{
    ifstream file(fName.c_str(), ifstream::ate | ifstream::binary);
    if (!file.is_open())
    {
        cout << "-------- File Open Failed --------" << endl;
        return -1;
    }
    cout << "-------- File Opend --------" << endl
         << flush;
    long len = file.tellg();
    file.close();
    return len;
}

int main()
{
    vector<string> args = readInfo();
    int portNo = stoi(args[0]);
    randomSeed = stoi(args[1]);
    srand(randomSeed);
    plp = stod(args[2]);
    int serverSocket, clientSocket;
    struct sockaddr_in serverAddress, clientAddress;
    int server_addrlen = sizeof(serverAddress);
    if ((serverSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
        cout << "-------- Create Socket Failed --------" << endl;
        return 1;
    }
    memset(&serverAddress, 0, sizeof(serverAddress));
    memset(&clientAddress, 0, sizeof(clientAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(portNo);
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    memset(&(serverAddress.sin_zero), '\0', AckPacketSize);
    if (bind(serverSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0)
    {
        cout << "-------- Bind Socket Failed --------" << endl;
        return 2;
    }
    while (true)
    {
        socklen_t clientAddressLength = sizeof(struct sockaddr);
        cout << "============ Started Connection ============" << endl;
        char rec_buffer[maxSegSize];
        ssize_t receivedBytes = recvfrom(serverSocket, rec_buffer, maxSegSize, 0, (struct sockaddr *)&clientAddress, &clientAddressLength);
        if (receivedBytes <= 0)
        {
            cout << "-------- Receive Failed --------" << endl;
            return 3;
        }
        // fork child procees to handle the request
        pid_t pid = fork();
        if (pid == -1)
        {
            cout << "-------- Forking Child Process Failed --------" << endl;
            return 4;
        }
        else if (pid == 0)
        {
            if ((clientSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
            {
                cout << "-------- Create Socket Failed --------" << endl;
                return 5;
            }
            handle_client_request(serverSocket, clientSocket, clientAddress, rec_buffer, maxSegSize);
            return 6;
        }
    }
    close(serverSocket);
    return 0;
}

/*
  send ack to file name
*/
void send_ack_file_name(int client_fd, string fName, int numberOfPackets, struct sockaddr_in client_addr)
{
    struct packetAck ack;
    ack.checkSum = 0;
    ack.len = numberOfPackets;
    ack.ackno = 0;
    char *buf = new char[maxSegSize];
    memset(buf, 0, maxSegSize);
    memcpy(buf, &ack, sizeof(ack));
    ssize_t bytesSent = sendto(client_fd, buf, maxSegSize, 0, (struct sockaddr *)&client_addr, sizeof(struct sockaddr));
    if (bytesSent == -1)
    {
        perror("-------- Error Sending The Ack --------");
        exit(1);
    }
    else
    {
        cout << "-------- Ack of file name is sent successfully --------" << endl
             << flush;
    }

    /** read data from file **/
    vector<string> dataPackets = readFileData(fName);
    if (dataPackets.size() == numberOfPackets)
    {
        cout << "-------- File Data Is Read Successfully --------" << endl
             << flush;
    }

    /** start sending data and handling congestion control using the SM **/
    sendTheData_HandleCongesion(client_fd, client_addr, dataPackets);
}

/*
   handle client request and send ack to file name
*/
void handle_client_request(int serverSocket, int client_fd, struct sockaddr_in client_addr, char rec_buffer[], int bufferSize)
{
    auto *data_packet = (struct packet *)rec_buffer;
    string fName = string(data_packet->data);
    cout << "-------- Requested File Name From Client  : " << fName << " --------" << endl
         << " , Length : " << fName.size() << " --------" << endl;
    int fileSize = checkFileExistence(fName);
    if (fileSize == -1)
    {
        return;
    }
    int numberOfPackets = ceil(fileSize * 1.0 / ChunkSize);
    cout << "-------- File Size : " << fileSize << " Bytes , Num. of chuncks : " << numberOfPackets << " --------" << endl
         << flush;

    send_ack_file_name(client_fd, fName, numberOfPackets, client_addr);
}

/*
   Handle time out
*/
bool handle_time_out(int client_fd, struct sockaddr_in client_addr, vector<string> data)
{
    bool entered = false;
    for (int j = 0; j < packetNotSents.size(); j++)
    {
        packetNotSent nspkt = packetNotSents[j];
        chrono::time_point<chrono::system_clock> current_time = chrono::system_clock::now();
        chrono::duration<double> elapsed_time = current_time - nspkt.timer;
        if (elapsed_time.count() >= 2)
        {
            entered = true;
            cout << "-------- Timed Out --------" << endl
                 << flush;
            cout << "-------- Re-transmitting the packet --------" << endl
                 << flush;
            SeqNum = nspkt.seqNo;
            string temp_packet_string = data[SeqNum];
            struct packet data_packet = createPacket(temp_packet_string, SeqNum);
            char sendBuffer[maxSegSize];
            memset(sendBuffer, 0, maxSegSize);
            memcpy(sendBuffer, &data_packet, sizeof(data_packet));
            ssize_t bytesSent = sendto(client_fd, sendBuffer, maxSegSize, 0, (struct sockaddr *)&client_addr, sizeof(struct sockaddr));
            if (bytesSent == -1)
            {
                perror("-------- Error Resending Packet --------");
                exit(1);
            }
            else
            {
                SentPacketsNotAcked++;
                AlreadySentPackets++;
                packetNotSents.erase(packetNotSents.begin() + j);
                j--;
                cout << "-------- Sent Seq Num : " << SeqNum << " --------" << endl
                     << flush;
            }
        }
    }
    return entered;
}

/*
  Handle Check Sum ;
*/
void handle_check_sum(bool found, int client_fd, struct sockaddr_in client_addr, vector<string> data)
{
    for (int j = 0; j < packetsSent.size(); j++)
    {
        packet spkt = packetsSent[j];
        if (spkt.seqNo == SeqNum)
        {
            found = true;
            string temp_packet_string = data[SeqNum];
            struct packet data_packet = createPacket(temp_packet_string, SeqNum);
            char sendBuffer[maxSegSize];
            memset(sendBuffer, 0, maxSegSize);
            memcpy(sendBuffer, &data_packet, sizeof(data_packet));
            ssize_t bytesSent = sendto(client_fd, sendBuffer, maxSegSize, 0, (struct sockaddr *)&client_addr, sizeof(struct sockaddr));
            if (bytesSent == -1)
            {
                perror("-------- Error Resending Data Packet --------");
                exit(1);
            }
            else
            {
                AlreadySentPackets++;
                packetsSent.erase(packetsSent.begin() + j);
            }
            break;
        }
    }
}

/*
   retransmit loss of packet
*/
bool retransmit_loss_packet(bool found, int client_fd, struct sockaddr_in client_addr, vector<string> data)
{

    for (int j = 0; j < packetNotSents.size(); j++)
    {
        packetNotSent nspkt = packetNotSents[j];
        if (nspkt.seqNo == SeqNum)
        {
            found = true;
            string temp_packet_string = data[SeqNum];
            struct packet data_packet = createPacket(temp_packet_string, SeqNum);
            char sendBuffer[maxSegSize];
            memset(sendBuffer, 0, maxSegSize);
            memcpy(sendBuffer, &data_packet, sizeof(data_packet));
            ssize_t bytesSent = sendto(client_fd, sendBuffer, maxSegSize, 0, (struct sockaddr *)&client_addr, sizeof(struct sockaddr));
            if (bytesSent == -1)
            {
                perror("-------- Error Resending Data Packet --------");
                exit(1);
            }
            else
            {
                SentPacketsNotAcked++;
                AlreadySentPackets++;
                packetNotSents.erase(packetNotSents.begin() + j);
            }
            break;
        }
    }

    return found;
}

/*
   Send Data and handle Congestion and retransmit loss packet
*/
void sendTheData_HandleCongesion(int client_fd, struct sockaddr_in client_addr, vector<string> data)
{
    ofstream myFile_Handler;
    // File Open
    myFile_Handler.open("Congestion_window.txt");

    int Cwnd_base = 0;
    double Cwnd = 1;
    // Write to the file
    myFile_Handler << Cwnd << endl;
    int TotalPackets = data.size();
    while (Flag)
    {
        /**
        this part will run first to send first datagram as stated in pdf.
        **/
        while (Cwnd_base < Cwnd && AlreadySentPackets + packetNotSents.size() < TotalPackets)
        {
            SeqNum = Base_packet_number + Cwnd_base;
            string temp_packet_string = data[SeqNum];
            /**
                in case error simulated won't send the packet so the seqnumber will not correct at the receiver so will send duplicate ack.
            **/
            bool isSent = send_packet(client_fd, client_addr, temp_packet_string, SeqNum);
            if (isSent == false)
            {
                perror("-------- Error Sending Data Packet --------");
            }
            else
            {
                SentPacketsNotAcked++;
                AlreadySentPackets++;
                cout << "-------- Sent Seq Num : " << SeqNum << " --------" << endl
                     << flush;
            }
            Cwnd_base++;
        }

        /*** receiving ACKs ***/
        if (SentPacketsNotAcked > 0)
        {
            StillExistAcks = true;
            while (StillExistAcks)
            {
                cout << "-------- Waiting acwnd_baseck "
                     << " --------" << endl
                     << flush;
                ssize_t receivedBytes = recvfrom(client_fd, Rec_buf, AckPacketSize, 0, (struct sockaddr *)&client_addr, &ClientAddressLength);
                if (receivedBytes < 0)
                {
                    perror("-------- Error Receiving Bytes --------");
                    exit(1);
                }
                else if (receivedBytes != AckPacketSize)
                {
                    cout << " -------- Expecting Ack Received Data Packet"
                         << " --------" << endl
                         << flush;
                    exit(1);
                }
                else
                {

                    auto ack = (packetAck *)malloc(sizeof(packetAck));
                    memcpy(ack, Rec_buf, AckPacketSize);
                    cout << "-------- Ack: " << ack->ackno << " Received --------" << endl
                         << flush;

                    if (getAckChecksum(ack->len, ack->ackno) != ack->checkSum)
                    {
                        cout << "-------- Corrupt Ack Received --------" << endl
                             << flush;
                    }

                    int ack_seqNo = ack->ackno;
                    if (LastAckedSeqNum == ack_seqNo)
                    {
                        NumberOfDupAcks++;
                        SentPacketsNotAcked--;
                        if (St == fastRecovery)
                        {
                            Cwnd++;
                            // Write to the file
                            myFile_Handler << Cwnd << endl;
                        }
                        else if (NumberOfDupAcks == 3)
                        {
                            Sst = Cwnd / 2;
                            Cwnd = Sst + 3;
                            cout << "******** Triple duplicate Ack ********" << endl;

                            // Write to the file
                            myFile_Handler << Cwnd << endl;
                            St = fastRecovery;
                            /** retransmit the lost packet **/
                            SeqNum = ack_seqNo;
                            bool found = false;
                            found = retransmit_loss_packet(found, client_fd, client_addr, data);
                            /** handle checksum error **/
                            if (!found)
                            {
                                handle_check_sum(found, client_fd, client_addr, data);
                            }
                        }
                    }
                    else if (LastAckedSeqNum < ack_seqNo)
                    {
                        /** new ack : compute new base and packet no. and handling congestion control FSM **/
                        cout << "-------- new Ack --------" << endl;
                        NumberOfDupAcks = 0;
                        LastAckedSeqNum = ack_seqNo;
                        int advance = LastAckedSeqNum - Base_packet_number;
                        Cwnd_base = Cwnd_base - advance;
                        Base_packet_number = LastAckedSeqNum;
                        if (St == slowStart)
                        {
                            if (Cwnd * 2 >= Sst)
                            {
                                St = congestionAvoidance;
                                Cwnd++;
                            }
                            else
                            {
                                Cwnd = Cwnd * 2;
                            }
                            // Write to the file
                            myFile_Handler << Cwnd << endl;
                            if (Cwnd >= Sst)
                            {
                                St = congestionAvoidance;
                            }
                        }
                        else if (St == congestionAvoidance)
                        {
                            Cwnd++;
                            // Write to the file
                            myFile_Handler << Cwnd << endl;
                        }
                        else if (St == fastRecovery)
                        {
                            St = congestionAvoidance;
                            Cwnd = Sst;
                            // Write to the file
                            myFile_Handler << Cwnd << endl;
                        }
                        SentPacketsNotAcked--;
                    }
                    else
                    {
                        SentPacketsNotAcked--;
                    }

                    if (SentPacketsNotAcked == 0)
                    {
                        StillExistAcks = false;
                    }
                }
            }
        }
        /** Handle Time Out **/
        bool entered = false;
        entered = handle_time_out(client_fd, client_addr, data);
        if (entered)
        {
            entered = false;
            Cwnd = 1;
            St = slowStart;
            // Write to the file
            myFile_Handler << Cwnd << endl;
        }
    }
    // File Close
    myFile_Handler.close();
}

/*
    in this function we Send packet data to client
*/
bool send_packet_data(bool corrupt, int client_fd, char sendBuffer[maxSegSize], struct sockaddr_in client_addr, struct packet data_packet, int seqNum)
{
    if (!corruptDatagram() && !corrupt)
    {
        ssize_t bytesSent = sendto(client_fd, sendBuffer, maxSegSize, 0, (struct sockaddr *)&client_addr, sizeof(struct sockaddr));
        if (bytesSent == -1)
        {
            return false;
        }
        else
        {
            packetsSent.push_back(data_packet);
            return true;
        }
    }
    else
    {
        cout << "******** Drop data ********" << endl;
        struct packetNotSent nspacket;
        nspacket.seqNo = seqNum;
        nspacket.isFinished = false;
        nspacket.timer = chrono::system_clock::now();
        packetNotSents.push_back(nspacket);

        return false;
    }
}

/*
  in this function we Send packets.
*/
bool send_packet(int client_fd, struct sockaddr_in client_addr, string temp_packet_string, int seqNum)
{
    char sendBuffer[maxSegSize];
    struct packet data_packet = createPacket(temp_packet_string, seqNum);
    bool corrupt = corruptDatagram();
    if (corrupt)
    {
        data_packet.checkSum = data_packet.checkSum - 1;
    }
    memset(sendBuffer, 0, maxSegSize);
    memcpy(sendBuffer, &data_packet, sizeof(data_packet));
    return send_packet_data(corrupt, client_fd, sendBuffer, client_addr, data_packet, seqNum);
}
