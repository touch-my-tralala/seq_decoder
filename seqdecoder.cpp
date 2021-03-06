#include "seqdecoder.h"

SeqDecoder::SeqDecoder(CodeRate code)
{
    m_code_type = code;
    if(code == Intelsat_1_2) m_coder_len = 36;
    else if(code == Intelsat_3_4) m_coder_len = 63;
    else if(code == Intelsat_7_8) m_coder_len = 89;
    // FIXME надо как-то по человечески сделать
    m_code_polynom.insert(Intelsat_1_2, "714461625313"); // oct
    m_code_polynom.insert(Intelsat_3_4, "736750426717050772741"); // oct
    m_code_polynom.insert(Intelsat_7_8, "776631661776007201537633372136"); // oct

    qint8 tap;
    for(auto j: qAsConst(m_code_polynom[code])){
        tap = static_cast<qint8>(j.digitValue());
        for(quint8 p=0; p<3; p++)
            m_taps_polynom.append( (tap>>(2-p)) & 1 );
    }
}


void SeqDecoder::setDefaultParams(const CodeRate &code){
    reset();
    m_delta_T = 5;
    setCodeType(code);
    m_back_step = 180;
    m_norm_thresh = 100;
}


void SeqDecoder::setDeltaT(quint8 deltaT){
    m_delta_T = deltaT;
}


void SeqDecoder::setCodeType(const CodeRate &code){
    reset();
    if(code == Intelsat_1_2) m_coder_len = 36;
    else if(code == Intelsat_3_4) m_coder_len = 63;
    else if(code == Intelsat_7_8) m_coder_len = 89;
    m_code_type = code;
    m_taps_polynom.clear();
    qint8 tap;
    for(auto j: qAsConst(m_code_polynom[code])){
        tap = static_cast<qint8>(j.digitValue());
        for(quint8 p=0; p<3; p++)
            m_taps_polynom.append( (tap>>(2-p)) & 1 );
    }
}


void SeqDecoder::setBackStep(quint16 backStep){
    if(backStep <= MAX_BACK_STEP - m_coder_len - 1)
        m_back_step = backStep;
    else
        qDebug() << "back step num >= (MAX_BACK_STEP - m_coder_len - 1)";
}


void SeqDecoder::setNormStep(quint16 normStep){
    if(normStep < MAX_BACK_STEP) m_norm_thresh = normStep;
    else qDebug() << "back step num >= MAX_BACK_STEP =" << MAX_BACK_STEP;
}


const QVector<quint8>& SeqDecoder::getDecodeData() const{
    if(m_decode_data.size() <= 0)
        qDebug() << "decode data shift reg is empty";
    return m_decode_data;
}


const QList<quint8>& SeqDecoder::scramblerV35(){
    m_descrembled_data.clear();
    if(m_dec_data.size() > 0){
        quint32 sreg = 0;
        quint8 x, y;
        for(auto i=0; i<m_dec_data.size(); i++){
            x = ((sreg >> 2) & 1) ^ ((sreg >> 19) & 1);
            y = x ^ m_dec_data[i];
            sreg <<= 1;
            sreg += m_dec_data[i];
            m_descrembled_data[i] = y ^ 1;
        }
    }else{
        qDebug() << "no decoded data";
    }
    return m_descrembled_data;
}


void SeqDecoder::reset(){
    m_sh_A.clear();
    m_sh_rib.clear();
    m_sh_mask.clear();
    m_dec_data.clear();
    m_decode_data.clear();

    m_Mp = -10000;
    m_Mc = 0;
    m_Ms = 0;
    m_forward_cnt = 0;
    m_pointer = 0;

}


bool SeqDecoder::addSymbs(const QList<quint8> &symbs){
     m_encode_data.clear();
     m_encode_data.append(symbs);
     return true;
}


// FIXME: не учитывает сдвиг маски при поиске синхронизации
void SeqDecoder::deperforate_data(){
    std::array<bool, 16> mask = {true, true,
                                 true, false,
                                 true, false,
                                 true, false,
                                 true, false,
                                 true, false,
                                 true, false};
    quint8 idx = 0;
    quint8 max_idx = 1;
    if(m_code_type == Intelsat_1_2)
        max_idx = 2;
    else if(m_code_type == Intelsat_3_4)
        max_idx = 6;
    else if(m_code_type == Intelsat_7_8)
        max_idx = 14;

    m_deperf_data.clear();
    m_perf_mask.clear();
    qint32 i = 0;
    while(i < m_encode_data.size()){
        if(mask[idx]){
            //m_deperf_data.append(m_encode_data[i]);
            m_deperf_data.append( (m_encode_data[i]&1) );
            m_perf_mask.append(true);
            i++;
        }else{
            m_deperf_data.append(0);
            m_perf_mask.append(false);
        }
        idx++;
        if(idx == max_idx)
            idx = 0;
   }
}


void SeqDecoder::decode(){
    deperforate_data();
    Rib rib;
    Mask mask;
    quint32 num = 0;
    auto k = m_perf_mask.begin();
    for(auto j = m_deperf_data.begin(); j != m_deperf_data.end(); j += 2){
        if(m_dec_data.size() < MAX_BACK_STEP){
            m_dec_data.prepend(*j);
            rib.b0 = *j;
            rib.b1 = *(j+1);
            m_sh_rib.prepend(rib);
            mask.b0 = *k;
            mask.b1 = *(k+1);
            m_sh_mask.prepend(mask);
            m_sh_A.prepend(false);
        }else{
            m_dec_data.prepend(*j);
            rib.b0 = *j;
            rib.b1 = *(j+1);
            m_sh_rib.prepend(rib);
            mask.b0 = *k;
            mask.b1 = *(k+1);           
            m_sh_mask.prepend(mask);
            m_sh_A.prepend(false);

            if(num==46099){
                qDebug();
            }

            seq_decode();
        }
        k += 2;

        if(num%1000 == 0)
            qDebug() << "decoding num =" << num << "symbs";
        num++;
    }
}


void SeqDecoder::seq_decode(){
    state = Idle;
    bool cycleEn = true;
    Rib rib0, rib1;
    qint16 metric;
    quint8 decSym;

    m_pointer++; // увеличение указателя на 1 т.к. пришел новый символ

    if(m_sh_rib.size() >= MAX_BACK_STEP){
        m_sh_rib.removeLast();
        m_sh_mask.removeLast();
        m_sh_A.removeLast();    
    }

    if(m_dec_data.size() >= MAX_BACK_STEP){
        m_decode_data.append(m_dec_data.last());
        m_dec_data.removeLast();
    }

    // для вычисления нового ребра используется pointer-1 т.к. новый декодированный символ еще не проверен и добавлен предварительно
    // для m_sh_mask, m_sh_A и m_sh_rib надо использовать pointer т.к. они добавляются в любом случае в начале fsm
    while(cycleEn){
        switch (state){
        case Idle:
            if(m_T >= m_norm_thresh){
                m_T /= 2;
                m_Mp /= 2;
                m_Mc /= 2; // m_Mc =  m_Mc / 2 + 1
            }

         // преднамеренный fallthrough в это состояние
         [[clang::fallthrough]];
         case MetricCalc:
            // Порождение двух возможных ребер
            recover_encoder(rib0, rib1, m_pointer);
            // Вычисление метрики между возможными ребрами и пришедшим ребром
            metric_calc(rib0, rib1, metric, decSym, m_pointer-1);
            m_Ms = m_Mc + metric;

            if(m_forward_cnt == 42)
                qDebug();


            if(m_Ms >= m_T){
                m_dec_data[m_pointer-1] = decSym;
                state = ForwardMove;
            }else if(m_Mp >= m_T){
                if(m_forward_cnt == 0)
                    qDebug() << "m_forward_cnt = 0";
                m_sh_A[m_pointer-1] = false;
                state = BackwardMove;
            }else{
                m_sh_A[m_pointer-1] = false;
                m_T -= m_delta_T;
                state = MetricCalc;
            }
            break;

        case ForwardMove:
            m_pointer--; // декодировали символ -> передвинули указатель на этот символ
            m_forward_cnt++;
            m_Mp = m_Mc;
            m_Mc = m_Ms;
            if(m_Mp < m_T + m_delta_T)
                m_T += m_delta_T;
            state = Idle;
            if(m_pointer == 0) // если декодировать больше нечего, то выходим из цикла
                cycleEn = false;
            break;

        case BackwardMove:
            m_pointer++; // отступаем назад и передвигаем указатель на предыдущий символ
            m_forward_cnt--;
            m_Mc = m_Mp;

            // Вычисление прошлого Mp
            if(m_pointer < m_back_step-3){ // еще есть шаги назад
                recover_encoder(rib0, rib1, m_pointer+1);
                metric_calc(rib0, rib1, metric, decSym, m_pointer); // FIXME проверить pointer-1 или pointer
                m_Mp -= metric;
            }else if(m_pointer == m_back_step-2){ // назад можно сделать еще 1 шаг
                m_Mp = 0;
            }else{ // больше идти назад нельзя
                m_Mp = -10000;
            }

            // Проверка: в прошлый раз ходили по худшему пути?
            // если да -> пробуем вернуться еще на шаг назад
            if(m_sh_A[m_pointer-1]){ // FIXME: pointer + 1  или может pointer?
                m_sh_A[m_pointer-1] = false;
                if(m_Mp >= m_T){
                    if(m_forward_cnt == 0)
                        qDebug() << "alarm m_forward_cnt = 0";
                    state = BackwardMove;
                }else{
                    m_T -= m_delta_T;
                    state = MetricCalc; // FIXME state = Idle ???
                }
            // если нет -> пробуем идти по худшему
            }else{
                m_sh_A[m_pointer-1] = true;
                state = MetricCalc;
            }

            break;
        }
   }
}


void SeqDecoder::metric_calc(Rib &rib0, Rib &rib1, qint16 &metric, quint8 &decSym, quint16 curPointer){
    quint8 hamm0 = hamming_distance(rib0, m_sh_rib[curPointer], m_sh_mask[curPointer]);
    quint8 hamm1 = hamming_distance(rib1, m_sh_rib[curPointer], m_sh_mask[curPointer]);

    if(hamm0 <= hamm1){
        metric = m_sh_A[curPointer] ? (hamm1 * (-5) + 1) : (hamm0 * (-5) + 1);
        decSym = m_sh_A[curPointer] ? 1 : 0;
    }else{
        metric = m_sh_A[curPointer] ? (hamm0 * (-5) + 1) : (hamm1 * (-5) + 1);
        decSym = m_sh_A[curPointer] ? 0 : 1;
    }
}


quint8 SeqDecoder::hamming_distance(Rib &rib0, Rib &rib1, Mask &mask){
    quint8 hamm = 0;
    if(rib0.b0 != rib1.b0)
        hamm = mask.b0 ? hamm + 1 : hamm;
    if(rib0.b1 != rib1.b1)
        hamm = mask.b1 ? hamm + 1 : hamm;
    return hamm;
}


void SeqDecoder::recover_encoder(Rib &rib0, Rib &rib1, quint16 curPointer){
    // FIXME добавить дифф. кодер

    QList<quint8> localDecData0(m_dec_data.mid(curPointer, m_coder_len-1));
    QList<quint8> localDecData1(m_dec_data.mid(curPointer, m_coder_len-1));
    localDecData0.prepend(0);
    localDecData1.prepend(1);

    rib0.b0 = 0;
    rib1.b0 = 1;

    rib0.b1 = 0;
    rib1.b1 = 0;
    for(qint32 j=0; j<m_taps_polynom.size(); j++){
        rib0.b1 ^= m_taps_polynom[j] & localDecData0[j];
        rib1.b1 ^= m_taps_polynom[j] & localDecData1[j];
    }
    qDebug();
}



