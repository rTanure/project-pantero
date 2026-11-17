#pragma once

// Quick Charge 3.0: pede 9 V ao carregador e mantém o pedido.
// A tensão é sempre 9 V; só o pedido é enviado, a tensão real não é medida pelo Uno.
namespace quick_charge {

// Handshake com o carregador + pausa + primeiro pedido de 9 V. Chamar no fim do setup().
void begin();

// Repete o pedido de 9 V a cada QC_REASSERT_MS enquanto o robô está parado (se o carregador perdeu o
// pedido ou voltou para 5 V). O pedido bloqueia ~60 ms, por isso não é feito durante a condução.
void update(bool robotIdle);

}  // namespace quick_charge
