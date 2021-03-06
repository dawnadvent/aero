#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "aero.common.h"

int checkServer()
{
    /*Verificar servidor*/
    if (access(SERVER,W_OK) == -1)
    {
        printf("Servidor não está em execução\n");
        return 0;
    }
    return 1;
}

void initRequest(pRequest p)
{
    p->pid = 0;
    p->isAdmin = 0;
    strcpy(p->command,"");
}

pRequest newRequest()
{
    pRequest new = malloc(sizeof(Request));
    initRequest(new);
    return new;
}

pRequest createClientPipe(int pid, int isAdmin)
{
    char pipeName[MAXPIPE];
    pRequest req = newRequest();
    req->pid = getpid();
    if (!isAdmin)
    {
        sprintf(pipeName,"%d",req->pid);
        mkfifo(pipeName,0644);
    }
    else
    {
        mkfifo(ADMIN,0644);
        req->isAdmin = 1;
    }
    return req;
}

void destroyClientPipe(pRequest req)
{
    char pipeName[MAXPIPE];
    if (!req) return;
    sprintf(pipeName,"%d",req->pid);
    unlink(pipeName);
    free(req);
}

void destroyAdminPipe(pRequest req)
{
    unlink(ADMIN);
    if (!req) return;
    free(req);
}

void sendRequestToServer(pRequest p)
{
    int server = open(SERVER,O_WRONLY);
    write(server,p,sizeof(Request));
    close(server);
}

pRequest doLoginWithRetry(char *username, char *password, int *retryCount)
{
    pRequest req;
    int client, isAdmin;
    Action resposta;
    char pipeName[MAXPIPE];
    /*Verificar ligação ao servidor*/
    if (checkServer() == 0)
      return NULL;
    
    /*Criar pipe actual*/
    req = createClientPipe(getpid(),strcmp(username,ADMIN) == 0);
    
    /*Dados para o pedido*/
    strcpy(req->command,"login");
    strcpy(req->username,username);
    strcpy(req->password,password);

    /*Enviar pedido ao servidor*/
    sendRequestToServer(req);
    
    /*Obter resposta do servidor*/
    resposta.idAction = 0;
    isAdmin = strcmp(username,ADMIN) == 0;
    if (isAdmin)
        sprintf(pipeName,"%s",username);
    else
        sprintf(pipeName,"%d",req->pid);
    
    client = open(pipeName,O_RDONLY);
    /*Ler resposta*/
    read(client,&resposta.idAction,sizeof(int));
    read(client,&resposta.hasText,sizeof(int));
    read(client,resposta.message,sizeof(resposta.message));
    /*Tratamento da resposta*/
    if (resposta.idAction == LOGIN_OK)
    {
        printf("Sessão iniciada: %s\n",resposta.message);
    }
    else
    {
        unlink(pipeName);
        free(req);
        req = NULL;
        printf("Login inválido: %s\n",resposta.message);
        /*No caso do Administrador*/
        if (isAdmin)
        {
            if (--(*retryCount) == 0)
                return NULL;
            printf("Tente novamente após %d segundos\n",LOGIN_WAIT);
            sleep(LOGIN_WAIT);
        }
    }
    close(client);
    return req;
}

pRequest doLogin(char *username, char *password)
{
    int dumb = 0;
    return doLoginWithRetry(username,password,&dumb);
}

void sendRequest(char *pipeClient, pRequest req, char *command, pAction resp)
{
    int client, i;
    /*Dados para o pedido*/
    strcpy(req->command,command);
    /*Enviar pedido ao servidor*/
    sendRequestToServer(req);
    /*Obter resposta do servidor*/
    resp->idAction = 0;
    /*Abrir pipe Cliente*/
    client = open(pipeClient,O_RDONLY);
    /*Ler resposta*/
    read(client,&resp->idAction,sizeof(int));
    read(client,&resp->hasText,sizeof(int));
    read(client,resp->message,sizeof(resp->message));
    
    /*Tem conteúdo extenso para devolver*/
    if (resp->hasText)
    {
        /*Número de Linhas*/
        read(client,&resp->totalLines,sizeof(int));
        
        resp->textLines = malloc(sizeof(char*) * resp->totalLines);
        for (i = 0; i < resp->totalLines; i++)
        {
            resp->textLines[i] = malloc(sizeof(char) * MAXMESSAGE);
            read(client,resp->textLines[i],sizeof(char) * MAXMESSAGE);
        }
    }
    /*Fechar pipe Cliente*/
    close(client);
    
    /*Login com sucesso?*/
    if (resp->idAction == LOGIN_FAILED)
        printf("%s\n",resp->message);
}

void sendRequestWithStatus(char *pipeClient, pRequest req, char *command, pAction resp)
{
    sendRequest(pipeClient,req,command,resp);
    /*Resposta*/
    if (resp->idAction == SUCCESS_REQ)
        printf("%s\n",MSG_COMMANDSUCCESS);
}

void sendRequestWithMessage(char *pipeClient, pRequest req, char *command, pAction resp)
{
    sendRequest(pipeClient,req,command,resp);
    /*Resposta*/
    if (resp->idAction == SUCCESS_REQ)
        printf("%s\n",resp->message);
}

void sendRequestWithFail(char *pipeClient, pRequest req, char *command, pAction resp)
{
    sendRequest(pipeClient,req,command,resp);
    /*Resposta*/
    switch (resp->idAction) {
        case SUCCESS_REQ:
            printf("%s\n",MSG_COMMANDSUCCESS);
            break;
        case FAILED_REQ:
            printf("%s:\n %s\n",MSG_COMMANDFAILED,resp->message);
            break;
        default:
            printf("idAction %d não implementado\n",resp->idAction);
            break;
    }
}

void sendRequestWithExtendedText(char *pipeClient, pRequest req, char *command, pAction resp)
{
    int i;
    sendRequest(pipeClient,req,command,resp);
    /*Resposta*/
    if (resp->idAction == SUCCESS_REQ)
    {
        printf("%s",resp->message);
        /*Verificar resposta extendida*/
        if (resp->hasText)
        {
            for (i = 0; i < resp->totalLines; i++)
                printf("%s",resp->textLines[i]);
            printf("\n");
        }
    }
}

int doLogout(pRequest req)
{
    Action resposta;
    char pipeName[MAXPIPE];
    char command[MAXCOMMAND];
    int isAdmin;
    if (!req) return 0;
    
    isAdmin = strcmp(req->username,ADMIN) == 0;
    if (isAdmin)
        sprintf(pipeName,"%s",req->username);
    else
        sprintf(pipeName,"%d",req->pid);
    
    strcpy(command,"logout");
    sendRequestWithMessage(pipeName,req,command,&resposta);
    return 1;
}