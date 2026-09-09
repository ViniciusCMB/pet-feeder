#include <ESP8266WiFi.h>
#include <DNSServer.h>          // Dependência do WiFiManager
#include <ESP8266WebServer.h>
#include <WiFiManager.h>        // Biblioteca para gerenciamento de conexão
#include <Servo.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <time.h>

// --- Configurações de Tempo (NTP) ---
const char* ntpServer = "a.st1.ntp.br";
const long gmtOffset_sec = -3 * 3600; 
const int daylightOffset_sec = 0;

// --- Componentes ---
Servo meuServo;
ESP8266WebServer server(80);
int pinoServo = 4; // D4 / GPIO2 — conforme README e hardware

// --- Variáveis de Controle ---
String horarios[3] = {"07:00", "13:00", "19:00"};
float tempoMotorLigado = 1.0; 
bool refeicaoServida[3] = {false, false, false};
int ultimoDiaDoAno = -1; 

void setup()
{
    Serial.begin(115200);

    // --- Inicialização dos Componentes ---
    meuServo.attach(pinoServo);
    meuServo.write(160); 
    delay(500);
    meuServo.detach(); 

    if (!SPIFFS.begin())
    {
        Serial.println("Erro ao montar o SPIFFS");
        return;
    }

    carregarConfiguracoes();

    // --- Gerenciador de Conexão Wi-Fi ---
    WiFiManager wifiManager;
    
    WiFi.hostname("AlimentadorPet");

    if (!wifiManager.autoConnect("AlimentadorPet-Setup")) {
        Serial.println("Falha ao conectar e o tempo de configuração expirou. Reiniciando...");
        delay(3000);
        ESP.restart();
    }

    Serial.println("\nWiFi conectado com sucesso!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Hostname: ");
    Serial.println(WiFi.hostname());

    // --- Sincronização da Hora Pela Internet ---
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.print("Sincronizando hora");
    while (time(nullptr) < 8 * 3600 * 2) {
        Serial.print(".");
        delay(500);
    }
    Serial.println("\nHora sincronizada!");

    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    ultimoDiaDoAno = timeinfo->tm_yday;

    // --- Endpoints do Servidor Web ---
    server.on("/", HTTP_GET, []() {
        File file = SPIFFS.open("/index.html", "r");
        if (!file) {
            server.send(404, "text/plain", "index.html nao encontrado");
            return;
        }
        server.streamFile(file, "text/html");
        file.close();
    });
    server.on("/settings", HTTP_GET, handleGetSettings);
    server.on("/save", HTTP_POST, handleSaveSettings);
    server.on("/test", HTTP_POST, handleTestMotor);

    server.begin();
}

void loop()
{
    server.handleClient();

    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);

    if (timeinfo->tm_yday != ultimoDiaDoAno)
    {
        Serial.println("Novo dia detectado! Reiniciando controle de refeições.");
        for (int i = 0; i < 3; i++)
        {
            refeicaoServida[i] = false;
        }
        ultimoDiaDoAno = timeinfo->tm_yday;
    }

    for (int i = 0; i < 3; i++)
    {
        int hora = horarios[i].substring(0, 2).toInt();
        int minuto = horarios[i].substring(3, 5).toInt();

        if (timeinfo->tm_hour == hora && timeinfo->tm_min == minuto && !refeicaoServida[i])
        {
            alimentar();
            refeicaoServida[i] = true;
        }
    }
    delay(1000); 
}

// --- Funções de Controle ---

void alimentar()
{
    meuServo.attach(pinoServo); 
    delay(50); 

    Serial.println("Liberando ração...");
    meuServo.write(0);
    delay(tempoMotorLigado * 1000);
    meuServo.write(160);
    delay(500);

    Serial.println("Ração liberada.");
    meuServo.detach(); 
}

// --- Funções do Servidor Web ---

void handleGetSettings()
{
    StaticJsonDocument<200> doc;
    doc["morning"] = horarios[0];
    doc["afternoon"] = horarios[1];
    doc["evening"] = horarios[2];
    doc["activeTime"] = tempoMotorLigado;

    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

void handleSaveSettings()
{
    if (server.hasArg("morning") && server.hasArg("afternoon") && server.hasArg("evening") && server.hasArg("activeTime"))
    {
        horarios[0] = server.arg("morning");
        horarios[1] = server.arg("afternoon");
        horarios[2] = server.arg("evening");
        tempoMotorLigado = server.arg("activeTime").toFloat();

        salvarConfiguracoes();
        server.send(200, "text/plain", "Configurações salvas com sucesso!");
    }
    else
    {
        server.send(400, "text/plain", "Faltando parâmetros");
    }
}

void handleTestMotor()
{
    if (server.hasArg("activeTime"))
    {
        float testTime = server.arg("activeTime").toFloat();
        Serial.print("Testando motor por ");
        Serial.print(testTime);
        Serial.println(" segundos.");

        meuServo.attach(pinoServo);
        delay(50);
        meuServo.write(0);
        delay(testTime * 1000);
        meuServo.write(160);
        delay(500);
        meuServo.detach();

        server.send(200, "text/plain", "Teste concluído");
    }
    else
    {
        server.send(400, "text/plain", "Faltando parâmetro 'activeTime'");
    }
}

// --- Funções de Arquivo (SPIFFS) ---

void salvarConfiguracoes()
{
    File arquivo = SPIFFS.open("/config.txt", "w");
    if (!arquivo)
    {
        Serial.println("Erro ao abrir config.txt para escrita");
        return;
    }
    arquivo.println(horarios[0]);
    arquivo.println(horarios[1]);
    arquivo.println(horarios[2]);
    arquivo.println(tempoMotorLigado);
    arquivo.close();
    Serial.println("Configurações salvas no SPIFFS.");
}

void carregarConfiguracoes()
{
    File arquivo = SPIFFS.open("/config.txt", "r");
    if (!arquivo)
    {
        Serial.println("Arquivo de config não encontrado, usando padrões.");
        return;
    }
    if (arquivo.available()) horarios[0] = arquivo.readStringUntil('\n');
    if (arquivo.available()) horarios[1] = arquivo.readStringUntil('\n');
    if (arquivo.available()) horarios[2] = arquivo.readStringUntil('\n');
    if (arquivo.available()) tempoMotorLigado = arquivo.readStringUntil('\n').toFloat();

    horarios[0].trim();
    horarios[1].trim();
    horarios[2].trim();

    arquivo.close();
    Serial.println("Configurações carregadas do SPIFFS.");
}
