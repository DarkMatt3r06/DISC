#include <stdio.h>
#include <pcap.h>
#include "encryptionFunctions.h"

#define ETHER_ADDR_LEN 6    // gli indirizzi MAC sono lunghi 6 byte
#define ETHER_ETYP_LEN 2    // il campo EtherType è lungo 2 byte
#define ETHER_HEAD_LEN 14   // l'header Ethernet è lungo 14 byte

#define packetHeader struct pcap_pkthdr

typedef struct mac_address {
    u_char addressBytes[ETHER_ADDR_LEN];
} mac_address;

typedef struct availableInterlocutor {
    char name[50];
    mac_address address;
} availableInterlocutor;

typedef struct availableInterlocutorsList {
    availableInterlocutor interlocutor;
    struct availableInterlocutorsList *next;
} availableInterlocutorsList;



mac_address ssapAddress; // indirizzo MAC del SSAP ( il mio indirizzo MAC )
mac_address dsap_address; // indirizzo MAC del DSAP ( il MAC della scheda di rete del destinatario )
availableInterlocutorsList *availableInterlocutorsHead = NULL; // lista dei dispositivi che hanno inviato RTCS

char *encryptionKey; // chiave di criptazione
char *encryptionSalt // sale di criptazione






//! === ADDRESS SETTING FUNCTIONS ===
void set_ssapAddress ( char *nicName ) {
    //. funzione che imposta l'indirizzo MAC del SSAP

    pcap_if_t *nicList;
    char errorBuffer[PCAP_ERRBUF_SIZE+1];
    
    // ottengo la lista delle NIC
    if ( pcap_findalldevs(&nicList , errorBuffer) == -1 ) {
        fprintf( stderr , "Error in pcap_findalldevs: %s\n" , errorBuffer );
        exit(1);
    }

    // cerco la NIC con il nome specificato
    pcap_if_t *currentDevice;
    for ( currentDevice=nicList ; currentDevice ; currentDevice=currentDevice->next ) {
        if ( strcmp(currentDevice->name , nicName) == 0 )
            break;
    }

    // se non ho trovato la NIC con il nome specificato, allora esco
    if ( currentDevice == NULL ) {
        fprintf( stderr , "Error: NIC not found\nRestart the program." );
        exit(1);
    }

    // copio l'indirizzo MAC della NIC nella variabile globale ssapAddress
    for ( int i=0 ; i<ETHER_ADDR_LEN ; i++ ) {
        ssapAddress.addressBytes[i] = currentDevice->addresses->addr->sa_data[i];
    }

    // libero la memoria allocata per la lista delle NIC
    pcap_freealldevs(nicList);

}

void set_dsapAddress ( u_char *packet ) {
    //. funzione che imposta l'indirizzo MAC del DSAP (un altro dispositivo) basandosi su un pacchetto ricevuto

    // copio l'indirizzo MAC del DSAP nella variabile globale dsapAddress
    for ( int i=0 ; i<ETHER_ADDR_LEN ; i++ ) {
        dsap_address.addressBytes[i] = packet[i+ETHER_ADDR_LEN];
    }

}






//! === NIC RELATED FUNCTIONS ===
void list_availableNICs () {
    //. funzione che elenca le NICs (network interface cards) disponibili

    pcap_if_t *nicList;
    char errorBuffer[PCAP_ERRBUF_SIZE+1];
    
    // ottengo la lista delle NIC
    if ( pcap_findalldevs(&nicList , errorBuffer) == -1 ) {
        fprintf( stderr , "Error in pcap_findalldevs: %s\n" , errorBuffer );
        exit(1);
    }

    // stampo le informazioni relative alle NIC trovate : ci interessa solo il nome
    for ( pcap_if_t *currentDevice=nicList ; currentDevice ; currentDevice=currentDevice->next ) {
    
        // stampa delle informazioni "basiche"
        printf( "%s\n" , currentDevice->name );
        if ( currentDevice->description )
            printf( "\tDescription: %s\n" , currentDevice->description );

        printf("\n");
    
    }

    // libero la memoria allocata per la lista delle NIC
    pcap_freealldevs(nicList);

}

pcap_t *open_NIC ( char *nicName ) {
    //. funzione che apre la scheda di rete specificata

    char errorBuffer[PCAP_ERRBUF_SIZE+1];

    // apro la scheda di rete specificata in modalità promiscua
    pcap_t *nicHandle = pcap_open_live( nicName , 65536 , 1 , 60000 , errorBuffer );
    if ( nicHandle != NULL ) {
        set_ssapAddress(nicName);
        return nicHandle;
    }

    // gestisco l'eventuale errore
    fprintf( stderr , "\nUnable to open the adapter. %s is not supported by WinPcap\n" , nicName );
    exit(1);

}

pcap_t *choose_NIC () { //tocheck
    //. funzione che stampa le NIC disponibili e chiede all'utente di sceglierne una

    // stampo le NIC disponibili
    printf("Available NICs:\n");
    list_availableNICs();

    // chiedo all'utente di scegliere una NIC
    char nicName[200];
    printf("Choose a NIC: ");
    fgets( nicName , 200 , stdin );

    // setto il terminatore al posto del carattere di newline
    for ( int i=0 ; i<200 ; i++ ) {
        if ( nicName[i] == '\n' ) {
            nicName[i] = '\0';
            break;
        }
    }

    // apro la NIC scelta e ne ritorno la handle
    return open_NIC(nicName);

}






//! === RTCS RELATED FUNCTIONS ===
void broadcast_RTCS ( pcap_t *nicHandle ) {
    //. funzione che "broadcasta" una RTCS sulla rete locale

    u_char packet[500];
    
    // setto il DSAP a 0xFF ( il pacchetto deve essere broadcastato )
    for ( int i=0 ; i<ETHER_ADDR_LEN ; i++ )
        packet[i] = 0xff;

    // setto il SSAP in modo tale che sia uguale al mio MAC
    for ( int i=0 ; i<ETHER_ADDR_LEN ; i++ )
        packet[i+ETHER_ADDR_LEN] = ssapAddress.addressBytes[i-ETHER_ADDR_LEN];
    
    // setto l'ethertype a quello usato per identificare l'applicazione
    packet[12] = 0x7a;
    packet[13] = 0xbc;

    // setto il primo byte a 0 ( per far riconoscere la RTCS )
    packet[14] = 0x00;

    // faccio scegliere all'utente il nome con cui i PC che ascoltano lo visualizzano
    char name[51]; // 50 caratteri + 1 per il terminatore
    printf("Choose a name (long between 10 and 50 characters): ");
    fgets( name , 51 , stdin );

    // setto il nome ed il terminatore
    for ( int i=0 ; i<50 ; i++ ) {
        if ( name[i] == '\n' ) {
            packet[15+i] = '\0';
            break;
        }
        packet[15+i] = name[i];
    }

    // invio il pacchetto
    int sendingResult = pcap_sendpacket( nicHandle , packet , 500 );
    if ( sendingResult == 0 )
        return;

    // gestisco l'eventuale errore
    fprintf( stderr , "\nError sending the packet: %s\nRestart the program." , pcap_geterr(nicHandle) );
    exit(1);

}

void list_availableInterlocutors ( pcap_t *nicHandle ) {
    //. funzione che elenca i dispositivi che hanno inviato RTCS

    int readingResult;
    packetHeader *header;
    const u_char *packetData;

    while ( (readingResult=pcap_next_ex( nicHandle , &header , &packetData )) >= 0 ) {
        if ( readingResult == 0 ) {
            printf("Timeout expired. Restart the program\n");
            exit(1);
        }



        //. controlli sulla validità del pacchetto
        // controllo che il pacchetto ricevuto sia un RTCS
        if ( packetData[12] != 0x7a || packetData[13] != 0xbc || packetData[14] != 0x00 )
            continue;
        
        // controllo che il pacchetto sia stato broadcastato
        if ( packetData[0] != 0xff || packetData[1] != 0xff || packetData[2] != 0xff || packetData[3] != 0xff || packetData[4] != 0xff || packetData[5] != 0xff )
            continue;

        // controllo che il pacchetto non sia stato inviato da me
        if ( memcmp( packetData+6 , ssapAddress.addressBytes , ETHER_ADDR_LEN ) == 0 )
            continue;



        //. operazioni da eseguire se il pacchetto è valido
        // stampo il nome e il MAC del dispositivo che ha broadcastato la RTCS
        printf( "%s : " , packetData+15 );
        for ( int i=0 ; i<ETHER_ADDR_LEN ; i++ ) {
        
            printf( "%02x" , packetData[i+6] );
            if ( i != ETHER_ADDR_LEN-1 )
                printf( ":" );

        }
        printf( "\n" );

        // aggiungo il dispositivo alla lista dei dispositivi disponibili ( in testa )
        availableInterlocutorsList *newInterlocutor = malloc( sizeof(availableInterlocutorsList) );
        for ( int i=0 ; i<50 ; i++ ) {

            if ( packetData[i+15] == '\0' ) {
                newInterlocutor->interlocutor.name[i] = '\0';
                break;
            }
            newInterlocutor->interlocutor.name[i] = packetData[i+15];
        
        }
        
        for ( int i=0 ; i<ETHER_ADDR_LEN ; i++ )
            newInterlocutor->interlocutor.address.addressBytes[i] = packetData[i+6];
        
        newInterlocutor->next = availableInterlocutorsHead;
        availableInterlocutorsHead = newInterlocutor;

    }

}

availableInterlocutor choose_availableInterlocutor () {
    //. funzione che chiede all'utente di scegliere un dispositivo tra quelli disponibili

    list_availableInterlocutors();

    // chiedo all'utente di scegliere un interlocutore in base al MAC address (è sicuramente univoco)
    char chosenAddressString[18];
    printf("Choose a device (by MAC address): ");
    fgets( chosenAddressString , 18 , stdin );

    mac_address chosenAddress;
    for ( int i=0 ; i<ETHER_ADDR_LEN ; i++ ) {
        sscanf( chosenAddressString+3*i , "%02x" , &chosenAddress.addressBytes[i] );
    }
        
    // cerco il dispositivo scelto nella lista dei dispositivi disponibili
    availableInterlocutorsList *currentInterlocutor;
    for ( currentInterlocutor=availableInterlocutorsHead ; currentInterlocutor ; currentInterlocutor=currentInterlocutor->next ) {
        if ( memcmp( currentInterlocutor->interlocutor.address.addressBytes , chosenAddress.addressBytes , ETHER_ADDR_LEN ) == 0 )
            break;
    }

    // se non ho trovato il dispositivo scelto, allora esco
    if ( currentInterlocutor == NULL ) {
        fprintf( stderr , "Error: device not found\nRestart the program." );
        exit(1);
    }

    // ritorno il dispositivo scelto
    set_dsapAddress( currentInterlocutor->interlocutor.address.addressBytes );
    return currentInterlocutor->interlocutor;

}






//! === STCS RELATED FUNCTIONS ===
void send_STCS ( pcap_t *nicHandle ) {
    //. funzione che invia una STCS al dispositivo specificato

    u_char packet[500];
    
    // setto il DSAP al MAC del dispositivo specificato
    for ( int i=0 ; i<ETHER_ADDR_LEN ; i++ )
        packet[i] = dsapAddress.addressBytes[i];

    // setto il SSAP in modo tale che sia uguale al mio MAC
    for ( int i=0 ; i<ETHER_ADDR_LEN ; i++ )
        packet[i+ETHER_ADDR_LEN] = ssapAddress.addressBytes[i-ETHER_ADDR_LEN];
    
    // setto l'ethertype a quello usato per identificare l'applicazione
    packet[12] = 0x7a;
    packet[13] = 0xbc;

    // setto il primo byte a 1 ( per far riconoscere la STCS )
    packet[14] = 0x01;

    // faccio scegliere all'utente il nome con cui l'interlocutore lo visualizzerà
    char name[51]; // 50 caratteri + 1 per il terminatore
    printf("Choose a name (long between 10 and 50 characters): ");
    fgets( name , 51 , stdin );

    // setto il nome ed il terminatore
    for ( int i=0 ; i<50 ; i++ ) {
        if ( name[i] == '\n' ) {
            packet[15+i] = '\0';
            break;
        }
        packet[15+i] = name[i];
    }

    // invio del pacchetto
    int sendingResult = pcap_sendpacket( nicHandle , packet , 500 );
    if ( sendingResult == 0 )
        return;

    // gestione dell'eventuale errore
    fprintf( stderr , "\nError sending the packet: %s\n" , pcap_geterr(nicHandle) );

}

void receive_STCS ( pcap_t *nicHandle ) {
    //. funzione che attende una STCS

    int readingResult;
    packetHeader *header;
    const u_char *packetData;

    while ( (readingResult=pcap_next_ex( nicHandle , &header , &packetData )) >= 0 ) {
        if ( readingResult == 0 ) {
            printf("Timeout expired. Restart the program\n");
            exit(0);
        }



        //. controlli sulla validità del pacchetto
        // controllo che il pacchetto sia di tipo STCS
        if ( packetData[12] != 0x7a || packetData[13] != 0xbc || packetData[14] != 0x01 ) {
            continue;
        }

        // controllo che il pacchetto sia per me
        if ( memcmp( packetData , ssapAddress.addressBytes , ETHER_ADDR_LEN ) != 0 ) {
            continue;
        }

        // controllo che il pacchetto sia stato inviato dal dispositivo scelto
        if ( memcmp( packetData+6 , dsapAddress.addressBytes , ETHER_ADDR_LEN ) != 0 ) {
            continue;
        }



        //. operazioni da eseguire se il pacchetto è valido
        // setto il DSAP al MAC del mittente
        set_dsapAddress( packetData+ETHER_ADDR_LEN );
        break;

    }

}






//! === ENCRYPTION SETTINGS RELATED FUNCTIONS ===
void send_encryptionKey ( pcap_t *nicHandle ) {
    //. funzione che genera ed invia la chiave di criptazione (+ il sale)

    // genero la chiave di criptazione
    encryptionKey = (char*) malloc( sizeof(char) * ( 33 ) );
    genereate_encryptionKey( encryptionKey , 33 );

    // genero il sale
    encryptionSalt = (char*) malloc( sizeof(char) * ( 5 ) );
    genereate_encryptionSalt( encryptionSalt );



    // invio la chiave di criptazione
    u_char packet[500];
    
    // setto il DSAP al MAC del dispositivo specificato
    for ( int i=0 ; i<ETHER_ADDR_LEN ; i++ )
        packet[i] = dsapAddress.addressBytes[i];

    // setto il SSAP in modo tale che sia uguale al mio MAC
    for ( int i=0 ; i<ETHER_ADDR_LEN ; i++ )
        packet[i+ETHER_ADDR_LEN] = ssapAddress.addressBytes[i-ETHER_ADDR_LEN];
    
    // setto l'ethertype a quello usato per identificare l'applicazione
    packet[12] = 0x7a;
    packet[13] = 0xbc;

    // setto il primo byte a 2 ( per far riconoscere la chiave di criptazione )
    packet[14] = 0x02;

    // copio la chiave di criptazione nel pacchetto
    for ( int i=0 ; i<32 ; i++ )
        packet[15+i] = encryptionKey[i];

    // copio il sale nel pacchetto
    for ( int i=0 ; i<32 ; i++ )
        packet[47+i] = encryptionSalt[i];

    // invio il pacchetto
    int sendingResult = pcap_sendpacket( nicHandle , packet , 500 );
    if ( sendingResult == 0 )
        return;

    // gestione dell'eventuale errore
    fprintf( stderr , "\nError sending the packet: %s\n" , pcap_geterr(nicHandle) );

}

//. funzione che attende la chiave di criptazione (+ il sale) e la salva nelle apposite variabili globali
void receive_encryptionKey ( pcap_t *nicHandle ) {

    int readingResult;
    packetHeader *header;
    const u_char *packetData;

    while ( (readingResult=pcap_next_ex( nicHandle , &header , &packetData )) >= 0 ) {
        if ( readingResult == 0 )
            printf("Timeout expired. Restart the program\n");
            exit(1);
        }



        //. controlli sulla validità del pacchetto
        // controllo che il pacchetto sia di tipo chiave di criptazione
        if ( packetData[12] != 0x7a || packetData[13] != 0xbc || packetData[14] != 0x02 ) {
            continue;
        }

        // controllo che il pacchetto sia per me
        if ( memcmp( packetData , ssapAddress.addressBytes , ETHER_ADDR_LEN ) != 0 ) {
            continue;
        }

        // controllo che il pacchetto sia stato inviato dal dispositivo scelto
        if ( memcmp( packetData+6 , dsapAddress.addressBytes , ETHER_ADDR_LEN ) != 0 ) {
            continue;
        }



        //. operazioni da eseguire se il pacchetto è valido
        // copio la chiave di criptazione nelle variabili globali
        encryptionKey = (char*) malloc( sizeof(char) * ( 33 ) );
        for ( int i=0 ; i<32 ; i++ )
            encryptionKey[i] = packetData[15+i];

        // copio il sale nelle variabili globali
        encryptionSalt = (char*) malloc( sizeof(char) * ( 5 ) );
        for ( int i=0 ; i<4 ; i++ )
            encryptionSalt[i] = packetData[47+i];

        break;

    }

}   






//! === CONNECTION VERIFICATION FUNCTIONS ===
void send_connectionVerificationPacket ( pcap_t *nicHandle ) {
    //. funzione che invia un pacchetto di verifica della connessione (ping)

    u_char packet[500];
    
    // setto il DSAP al MAC del dispositivo specificato
    for ( int i=0 ; i<ETHER_ADDR_LEN ; i++ )
        packet[i] = dsapAddress.addressBytes[i];

    // setto il SSAP in modo tale che sia uguale al mio MAC
    for ( int i=0 ; i<ETHER_ADDR_LEN ; i++ )
        packet[i+ETHER_ADDR_LEN] = ssapAddress.addressBytes[i-ETHER_ADDR_LEN];
    
    // setto l'ethertype a quello usato per identificare l'applicazione
    packet[12] = 0x7a;
    packet[13] = 0xbc;

    // setto il primo byte a 3 ( per far riconoscere il pacchetto di verifica della connessione )
    packet[14] = 0x03;

    // invio il pacchetto
    int sendingResult = pcap_sendpacket( nicHandle , packet , 500 );
    if ( sendingResult == 0 )
        return;

    // non gestisco l'eventuale errore perchè stamperebbe un messaggio di errore che offuscherebbe la chat

}

void receive_connectionVerificationPacket ( pcap_t *nicHandle ) {
    //. funzione che attende un pacchetto di verifica della connessione (pong)

    int readingResult;
    packetHeader *header;
    const u_char *packetData;

    while ( (readingResult=pcap_next_ex( nicHandle , &header , &packetData )) >= 0 ) {
        if ( readingResult == 0 ) {
            printf("Timeout expired. The other device is not responding.\n");
            exit(1);
        }



        //. controlli sulla validità del pacchetto
        // controllo che il pacchetto sia di tipo verifica della connessione
        if ( packetData[12] != 0x7a || packetData[13] != 0xbc || packetData[14] != 0x03 ) {
            continue;
        }

        // controllo che il pacchetto sia per me
        if ( memcmp( packetData , ssapAddress.addressBytes , ETHER_ADDR_LEN ) != 0 ) {
            continue;
        }

        // controllo che il pacchetto sia stato inviato dal dispositivo scelto
        if ( memcmp( packetData+6 , dsapAddress.addressBytes , ETHER_ADDR_LEN ) != 0 ) {
            continue;
        }

        break;

    }

}






//! === CHAT FUNCTIONS ===
void send_message ( pcap_t *nicHandle , char *message ) {
    //. funzione che invia un messaggio dopo averlo criptato

    u_char packet[500];

    // setto il DSAP al MAC del dispositivo specificato
    for ( int i=0 ; i<ETHER_ADDR_LEN ; i++ )
        packet[i] = dsapAddress.addressBytes[i];

    // setto il SSAP in modo tale che sia uguale al mio MAC
    for ( int i=0 ; i<ETHER_ADDR_LEN ; i++ )
        packet[i+ETHER_ADDR_LEN] = ssapAddress.addressBytes[i-ETHER_ADDR_LEN];

    // setto l'ethertype a quello usato per identificare l'applicazione
    packet[12] = 0x7a;
    packet[13] = 0xbc;

    // setto il primo byte a 4 ( per far riconoscere il messaggio )
    packet[14] = 0x04;

    // cripto il messaggio
    encrypt_string( message , encryptionKey , encryptionSalt );

    // copio il messaggio nel pacchetto
    for ( int i=0 ; i<strlen(message) ; i++ )
        packet[15+i] = message[i];

    // invio il pacchetto
    int sendingResult = pcap_sendpacket( nicHandle , packet , 500 );
    if ( sendingResult == 0 )
        return;

    // gestione dell'eventuale errore
    fprintf( stderr , "\nError sending the packet: %s\n" , pcap_geterr(nicHandle) );

}

void receiveAndPrint_message ( pcap_t *nicHandle ) {
    //. funzione che attende un messaggio e lo stampa dopo averlo decriptato

    int readingResult;
    packetHeader *header;
    const u_char *packetData;

    while ( (readingResult=pcap_next_ex( nicHandle , &header , &packetData )) >= 0 ) {
        if ( readingResult == 0 ) {
            printf("Timeout expired. Restart the program\n");
            exit(1);
        }



        //. controlli sulla validità del pacchetto
        // controllo che il pacchetto sia di tipo messaggio
        if ( packetData[12] != 0x7a || packetData[13] != 0xbc || packetData[14] != 0x04 ) {
            continue;
        }

        // controllo che il pacchetto sia per me
        if ( memcmp( packetData , ssapAddress.addressBytes , ETHER_ADDR_LEN ) != 0 ) {
            continue;
        }

        // controllo che il pacchetto sia stato inviato dal dispositivo scelto
        if ( memcmp( packetData+6 , dsapAddress.addressBytes , ETHER_ADDR_LEN ) != 0 ) {
            continue;
        }



        //. operazioni da eseguire se il pacchetto è valido
        // decripto il messaggio
        char *decryptedMessage = (char*) malloc( sizeof(char) * ( strlen(packetData+15) + 1 ) );
        encrypt_string( packetData+15 , encryptionKey , encryptionSalt );

        // stampo il messaggio
        printf( "%s\n" , decryptedMessage );

        break;

    }

}