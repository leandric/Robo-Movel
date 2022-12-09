/* Q0357
 * Autor - Leandro S. Silva
 * Baseado no projeto desenvolvido pelo canal "brincando com ideias" - https://www.youtube.com/watch?v=vQXwtEf5V1I&t=311s
 * ⚙️BIBLIOTECA (Adafruit Motor Shield):
 * 
 * sudo groupadd dialout
sudo gpasswd -a USER dialout
sudo usermod -a -G dialout USER
sudo chmod a+rw /dev/ttyACM0
 */


#include <AFMotor.h>        // INCLUI A BIBLIOTECA DA MOTOR SHIELD

#define FORWARD 1 //VALOR DA BIBLIOTECA 1
#define BACKWARD 2 //VALOR DA BIBLIOTECA 2

// INSTANCIANDO MOTORES
AF_DCMotor motor1(1);
AF_DCMotor motor2(2);
AF_DCMotor motor3(3);
AF_DCMotor motor4(4);

//variavel de
int bit1;
int bit2;
int bit3;
int bit4;

//Pinos digitais para os comandos do esp32-cam
int pin1 = A0;
int pin2 = A1;
int pin3 = A2;
int pin4 = A3;

// VARIÁVEL PARA RECEBER COMANDO DO BLUETOOTH
char comando = 0 ;
// VARIÁVEL PARA ARMAZENAR DISTANCIA MEDIDA NO SENSOR
float distancia;

//Configuração
void setup() {
  // VELOCIDADE DOS MOTORES
  motor1.setSpeed(255);  // 255 VELOCIDADE MAXIMA
  motor2.setSpeed(255);  // 255 VELOCIDADE MAXIMA
  motor3.setSpeed(255);  // 255 VELOCIDADE MAXIMA
  motor4.setSpeed(255);  // 255 VELOCIDADE MAXIMA

  // MOTORES INICIAM PARADOS
  motor1.run(RELEASE);
  motor2.run(RELEASE);
  motor3.run(RELEASE);
  motor4.run(RELEASE);

  //Pinos de leitura de comando
  Serial.begin(9600);
  pinMode(pin1, INPUT);
  pinMode(pin2, INPUT);
  pinMode(pin3, INPUT);
  pinMode(pin4, INPUT);
  
}

// Função principal
void loop() {
  valorComando();

}
void mFrente() {
  motor1.run(FORWARD);
  motor2.run(FORWARD);
  motor3.run(FORWARD);
  motor4.run(FORWARD);
}
void mTras() {
  motor1.run(BACKWARD);
  motor2.run(BACKWARD);
  motor3.run(BACKWARD);
  motor4.run(BACKWARD);
}
void mDireita() {
  motor1.run(BACKWARD);
  motor2.run(BACKWARD);
  motor3.run(FORWARD);
  motor4.run(FORWARD);
}
void mEsquerda() {
  motor1.run(FORWARD);
  motor2.run(FORWARD);
  motor3.run(BACKWARD);
  motor4.run(BACKWARD);
}
void ficarParado() {
  motor1.run(RELEASE);
  motor2.run(RELEASE);
  motor3.run(RELEASE);
  motor4.run(RELEASE);
}
void giroDireta() {
  motor1.run(FORWARD);
  motor2.run(BACKWARD);
  motor3.run(BACKWARD);
  motor4.run(FORWARD);
}
void giroEsquerda() {
  motor1.run(BACKWARD);
  motor2.run(FORWARD);
  motor3.run(FORWARD);
  motor4.run(BACKWARD);
}

//Convert o sinal digital de 8 bits em um comando int
int valorComando(){
  if(digitalRead(pin1)== HIGH){
    bit1=1;    
  }else{
    bit1=0;
  }

  if(digitalRead(pin2)== HIGH){
    bit2=1;    
  }else{
    bit2=0;
  }

  if(digitalRead(pin3)== HIGH){
    bit3=1;    
  }else{
    bit3=0;
  }

  if(digitalRead(pin4)== HIGH){
    bit4=1;    
  }else{
    bit4=0;
  }
  
  if (bit4==0 && bit3==0 && bit2==0 && bit1==0) {
    
    Serial.println("aguardando comando...");
    return 0;
  
	} else if (bit4==0 && bit3==0 && bit2==0 && bit1==1) {
      mFrente();
      Serial.println("Frente");
      return 1;
	} else if (bit4==0 && bit3==0 && bit2==1 && bit1==0) {
      giroDireta();
      Serial.println("Giro para Direita");
      return 2;
  	} else if (bit4==0 && bit3==0 && bit2==1 && bit1==1) {
      ficarParado();
      Serial.println("Parar");
      return 3;
  	}else if (bit4==0 && bit3==1 && bit2==0 && bit1==0) {
      giroEsquerda();
      Serial.println("Giro para Esquerda");
      return 4;
  	}else if (bit4==0 && bit3==1 && bit2==0 && bit1==1) {
      mTras();
      Serial.println("Ré");
      return 5;
  	}else if (bit4==0 && bit3==1 && bit2==1 && bit1==0) {
      mDireita();
      Serial.println("Direita");
      return 6;
  	}else if (bit4==0 && bit3==1 && bit2==1 && bit1==1) {
      mEsquerda();
      Serial.println("Esquerda");
      return 7;
  	}else if (bit4==1 && bit3==0 && bit2==0 && bit1==0) {
      
      return 8;
  	}else if (bit4==1 && bit3==0 && bit2==0 && bit1==1) {
      
      return 9;
  	}else if (bit4==1 && bit3==0 && bit2==1 && bit1==0) {
      
      return 10;
    }else if (bit4==1 && bit3==0 && bit2==1 && bit1==1) {
      
      return 11;
    }else if (bit4==1 && bit3==1 && bit2==0 && bit1==0) {
      
      return 12;
    }else if (bit4==1 && bit3==1 && bit2==0 && bit1==1) {
      
      return 13;
    }else if (bit4==1 && bit3==1 && bit2==1 && bit1==0) {
      
      return 14;
    }else if (bit4==1 && bit3==1 && bit2==1 && bit1==1) {
      
      return 15;
    }
}
