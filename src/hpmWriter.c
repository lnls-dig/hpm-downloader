#include <mtca.h>
#include <unistd.h>
#include <hpmWriter.h>

img_info_t img_info;

int hpmdownload(unsigned char *byte, unsigned int filesize, unsigned char *ip, unsigned char *username, unsigned char *password, unsigned char slot, unsigned int comp, bool retries, bool check_component)
{
    unsigned char i;

    printf("\n[INFO] \t {main} \t\t\t Programming MMC slot %d \n",slot);

    switch(get_img_information(byte, filesize, check_component)){
    case 0xFF:  printf("[ERROR]  {get_img_information} \t\t HPM image header failed \n");       return -1;
    case 0xFE:  printf("[ERROR]  {get_img_information} \t\t HPM image format version failed \n");       return -1;
    case 0xFD:  printf("[ERROR]  {get_img_information} \t\t HPM image checksum error \n");      return -1;
    case 0xFC:  printf("[ERROR]  {get_img_information} \t\t HPM image action checksum error \n");       return -1;
    case 0xFB:  printf("[ERROR]  {get_img_information} \t\t Upgrade action should affect only one component \n");       return -1;
    default: printf("[INFO] \t {get_img_information} \t\t HPM image check successful \n");
    }

    switch(check_hpm_info(ip, username, password, slot)){
    case 0xFF:  printf("[ERROR]  {check_hpm_info} \t\t Send GET_DEVICE_ID failed \n");  return -1;
    case 0xFE:  printf("[ERROR]  {check_hpm_info} \t\t Completion code error (expected 0x00) \n");      return -1;
    case 0xFD:  printf("[ERROR]  {check_hpm_info} \t\t Read data length error (expected 11 bytes) \n"); return -1;
    case 0xFC:  printf("[ERROR]  {check_hpm_info} \t\t Product id not compatible with HPM image \n");   return -1;
    case 0xFB:  printf("[ERROR]  {check_hpm_info} \t\t Manufacturer id not compatible with HPM image \n");      return -1;
    case 0xFA:  printf("[ERROR]  {check_hpm_info} \t\t Current MMC version < than HPM image's earliest compatible version \n"); return -1;
    case 0xF9:  printf("[ERROR]  {check_hpm_info} \t\t Send GET_TARGET_UPGRADE_CAPABILITIES failed \n");        return -1;
    case 0xF8:  printf("[ERROR]  {check_hpm_info} \t\t Read data length error (expected 7 bytes) \n");  return -1;
    case 0xF7:  printf("[ERROR]  {check_hpm_info} \t\t HPM.1 not supported \n");        return -1;
    case 0xF6:  printf("[ERROR]  {check_hpm_info} \t\t Firmware upgrade is not desirable at this time \n");     return -1;
    case 0xF5:  printf("[ERROR]  {check_hpm_info} \t\t MMC's capabilities differ with HPM image \n");   return -1;
    case 0xF4:  printf("[ERROR]  {check_hpm_info} \t\t Component(s) not present \n");   return -1;
    default: printf("[INFO] \t {check_hpm_info} \t\t HPM image check successful \n");
    }

    for(i=0; i < img_info.nb_actions; i++){
        if(img_info.actions[i].action == 0x02){
            switch(hpm_upgrade(ip, username, password, slot, &img_info.actions[i], byte, img_info.components, retries)){
            case 0xFF: printf("[ERROR]  {Upgrade action} \t\t Initiate upgrade action failed \n");      return -1;
            case 0xFE: printf("[ERROR]  {Upgrade action} \t\t Completion code error \n");       return -1;
            case 0xFD: printf("[ERROR]  {Upgrade action} \t\t Get upgrade status failed \n");   return -1;
            case 0xFC: printf("[ERROR]  {Upgrade action} \t\t Timeout \n");     return -1;
            case 0xFB: printf("[ERROR]  {Upgrade action} \t\t Upload firmware block failed \n");        return -1;
            case 0xFA: printf("[ERROR]  {Upgrade action} \t\t Upgrade failed \n");      return -1;
            case 0xF9: printf("[ERROR]  {Upgrade action} \t\t Finish firmware upload failed \n");       return -1;
            case 0xF8: printf("[ERROR]  {Upgrade action} \t\t Upgrade failed (size error) \n"); return -1;
            default: printf("[INFO] \t {Upgrade action} \t\t Upgrade success \n");
            }
        }
    }
    return 0x00;
}

unsigned char get_img_information(unsigned char *byte, unsigned int binsize, bool check_component) {

    unsigned char i;
    unsigned char crc=0;

    //Check header
    if( byte[0] != 0x50 ||
        byte[1] != 0x49 ||
        byte[2] != 0x43 ||
        byte[3] != 0x4D ||
        byte[4] != 0x47 ||
        byte[5] != 0x46 ||
        byte[6] != 0x57 ||
        byte[7] != 0x55) return 0xFF;

    //Check format version
    if(byte[8] != 0x00) return 0xFE;

    //Check checksum
    for(i=0; i<33; i++){
        crc -= byte[i];
    }

    if(crc != byte[34]) return 0xFD;

    img_info.device_id = byte[9];
    img_info.manufacturer_id[0] = byte[10];     //LSB
    img_info.manufacturer_id[1] = byte[11];
    img_info.manufacturer_id[2] = byte[12];     //MSB
    img_info.product_id[0] = byte[13];  //LSB
    img_info.product_id[1] = byte[14];  //MSB
    img_info.image_capabilities = byte[19];
    img_info.components = byte[20];
    img_info.self_test_timeout = byte[21];
    img_info.rollback_timeout = byte[22];
    img_info.inaccessibility_timeout = byte[23];
    img_info.earliest_compatibility_vers[0] = byte[24];
    img_info.earliest_compatibility_vers[1] = byte[25];
    img_info.firware_rev[0] = byte[26];
    img_info.firware_rev[1] = byte[27];
    img_info.firware_rev[2] = byte[28];
    img_info.firware_rev[3] = byte[29];
    img_info.firware_rev[4] = byte[30];
    img_info.firware_rev[5] = byte[31];
    img_info.check_component = check_component;

    img_info.oem_data_len = (unsigned short)byte[32];
    img_info.oem_data_len += ((unsigned short)byte[33]) * 256;

    return get_action(byte, binsize);
}

unsigned char check_hpm_info(unsigned char *ip, unsigned char *username, unsigned char *password, unsigned char amc_slot_number)
{
    unsigned char len, i, offset;

    unsigned char data[25];
    struct ipmi_rs *rsp;

    struct ipmi_intf *intf = open_lan_session(ip,
                                              username,
                                              password,
                                              (0x70+2*amc_slot_number),                 //No target specified (Default: MCH)
                                              0x82,                                     //No transit addr specified (Default: 0)
                                              7,                                        //No target channel specified (Default: 0)
                                              0);

    rsp = send_ipmi_cmd(intf, 0x06, 0x01, NULL, 0);
    if(rsp == NULL) {
        intf->close(intf);
        return 0xFF;
    } else {
        printf("[INFO] \t {GET_DEVICE_ID} \t\t Completion Code : 0x%02x \n", rsp->ccode);
        if(rsp->ccode) {
            printf("[INFO] \t {GET_DEVICE_ID} \t\t Completion Code : 0x%02x \n", rsp->ccode);
            intf->close(intf);
            return 0xFE;
        }

        if(rsp->data_len != 11){
            intf->close(intf);
            return 0xFD;
        }

        if(rsp->data[9] != img_info.product_id[0] || rsp->data[10] != img_info.product_id[1]){
            intf->close(intf);
            return 0xFC;
        }  //Check product ID

        if(rsp->data[6] != img_info.manufacturer_id[0] || rsp->data[7] != img_info.manufacturer_id[1] || rsp->data[8] != img_info.manufacturer_id[2]) {
            intf->close(intf);
            return 0xFB;
        }//Check Manufacturer ID

        if(rsp->data[2] < img_info.earliest_compatibility_vers[0] || (rsp->data[2] == img_info.earliest_compatibility_vers[0] && rsp->data[3] < img_info.earliest_compatibility_vers[1])) {
            intf->close(intf);
            return 0xFA;
        }//Check vers.
    }

    printf("[INFO] \t {check_hpm_info} \t\t version %d.%d will be replace by %d.%d \n", rsp->data[2], rsp->data[3], img_info.firware_rev[0], img_info.firware_rev[1]);

    rsp = send_ipmi_cmd(intf, 0x2c, 0x2E, NULL, 0);
    if(rsp == NULL){
        intf->close(intf);
        return 0xF9;
    }else{
        if(rsp->ccode){
            printf("[INFO] \t {GET_TARGET_UPGRADE_CAPABILITIES} \t Completion Code : 0x%02x \n", rsp->ccode);
            intf->close(intf);
            return 0xFE;
        }

        if(rsp->data_len != 8){
            intf->close(intf);
            return 0xF8;
        }

        if(rsp->data[1] != 0x00){
            intf->close(intf);
            return 0xF7;
        }//HPM.1 not supported

        if(rsp->data[2] & 0x01){
            intf->close(intf);
            return 0xF6;
        }//Firmware upgrade is not desirable at this time

        //img_info.image_capabilities
        //      Byte [2] : Manual roll-back capabilities
        //                                      0b = Not supported
        //                                      1b = Supported
        //      Byte [1] : Automatic roll-back capabilities
        //                                      0b = Not supported
        //                                      1b = Supported
        //      Byte [2] : Self-test capabilities
        //                                      0b = Not supported
        //                                      1b = Supported
        //
        //GET_TARGET_UPGRADE_CAPABILITIES - rsp->data[2]
        //      Byte [2] : Manual roll-back capabilities
        //                                      0b = Not supported
        //                                      1b = Supported
        //      Byte [1] : Automatic roll-back capabilities
        //                                      0b = Not supported
        //                                      1b = Supported
        //      Byte [2] : Self-test capabilities
        //                                      0b = Not supported
        //                                      1b = Supported

        if((rsp->data[2] & 0x07) != (img_info.image_capabilities & 0x07)) {
            intf->close(intf);
            return 0xF5;
        }//Capabilities are different between HPM image and MMC's information
        if((rsp->data[7] & img_info.components) !=  img_info.components) {
            intf->close(intf);
            return 0xF4;
        }//Component(s) not present

        img_info.upgrade_timeout = rsp->data[3]; //5 second per unit
    }

    intf->close(intf);
}

unsigned char get_action(unsigned char *byte, unsigned int binsize)
{
    unsigned int offset = 35 + img_info.oem_data_len;
    unsigned char chksum, i, j;

    img_info.nb_actions = 0;

    for(i=0; i < MAX_ACTION && offset < (binsize-16); i++){
        img_info.actions[i].action = byte[offset++];
        img_info.actions[i].components = byte[offset++];

        chksum = 0 - img_info.actions[i].action - img_info.actions[i].components;

        if(byte[offset] != chksum){
            printf("[INFO] \t {get_action} \t\t\t checksum 0x%02x (expected 0x%02x) \n",byte[offset], chksum);
            return 0xFC;
        }
        offset++;

        switch(img_info.actions[i].action){
        case 0x00: printf("[INFO] \t {Action detected} \t\t Backup component (Not implemented yet) \n");        break;
        case 0x01: printf("[INFO] \t {Action detected} \t\t Prepare component (Not implemented yet) \n");       break;
        case 0x02: printf("[INFO] \t {Action detected} \t\t Upload firmware image \n"); break;
        default: printf("[INFO] \t {Upgrade action detected} \t Unknown action \n");    break;
        }

        if(img_info.actions[i].action == 0x02){
            if(img_info.check_component){
                switch(img_info.actions[i].components){
                case 1:   printf("[INFO] \t {Upgrade action detected} \t Upgrade for component 0 \n"); break;
                case 2:   printf("[INFO] \t {Upgrade action detected} \t Upgrade for component 1 \n"); break;
                case 4:   printf("[INFO] \t {Upgrade action detected} \t Upgrade for component 2 \n"); break;
                case 8:   printf("[INFO] \t {Upgrade action detected} \t Upgrade for component 3 \n"); break;
                case 16:  printf("[INFO] \t {Upgrade action detected} \t Upgrade for component 4 \n"); break;
                case 32:  printf("[INFO] \t {Upgrade action detected} \t Upgrade for component 5 \n"); break;
                case 64:  printf("[INFO] \t {Upgrade action detected} \t Upgrade for component 6 \n"); break;
                case 128: printf("[INFO] \t {Upgrade action detected} \t Upgrade for component 7 \n"); break;
                default:  printf("[INFO] \t {Upgrade action} \t\t Components value : 0x%02x \n", img_info.actions[i].components); return 0xFB;
                }
            }

            printf("[INFO] \t {Upgrade action detected} \t Upgrade to version %d.%d \n",byte[offset++], byte[offset++]);
            offset += 4;

            printf("[INFO] \t {Upgrade action detected} \t \"");
            for(j=0; j < 21 && byte[offset+j] != 0; j++)
                printf("%c",byte[offset+j]);
            printf("\" firmware \n");
            offset += 21;

            img_info.actions[i].firmware_length = (unsigned int)byte[offset++];
            img_info.actions[i].firmware_length += ((unsigned int)byte[offset++]) * 256;
            img_info.actions[i].firmware_length += ((unsigned int)byte[offset++]) * 65536;
            img_info.actions[i].firmware_length += ((unsigned int)byte[offset++]) * 16777216;

            img_info.actions[i].data_offset = offset;
            offset += img_info.actions[i].firmware_length;
        }

        img_info.nb_actions++;
    }

    return 0x00;

}

unsigned char hpm_upgrade(unsigned char *ip, unsigned char *username, unsigned char *password, unsigned char amc_slot_number, action_t *action, unsigned char *byte, unsigned int component, bool retries){
    unsigned char len, i;
    unsigned int timeout;
    unsigned char ccode;
    unsigned int offset;
    unsigned char scan_ret;

    unsigned char data[DATA_PER_BLOCK+2];
    unsigned char block_nb;

    struct ipmi_rs *rsp;

    struct ipmi_intf *intf = open_lan_session(ip,
                                              username,
                                              password,
                                              (0x70+2*amc_slot_number),                 //No target specified (Default: MCH)
                                              0x82,                                                             //No transit addr specified (Default: 0)
                                              7,                                                                        //No target channel specified (Default: 0)
                                              0);
    //Initiate upgrade action
    data[0] = 0x00;                                             //PICMG ID
    data[1] = component; //action-> components;              //Component (only one for upgrade action)
    data[2] = 0x02;                                             //Upload for upgrade action

    rsp = send_ipmi_cmd(intf, 0x2c, 0x31, data, 3);
    if(rsp == NULL){
        intf->close(intf);
        return 0xFF;
    }else{
        if(rsp->ccode != 0x00 && rsp->ccode != 0x80){   //Long action is in progress
            printf("[INFO] \t {INITIATE_UPGRADE_ACTION} \t Completion Code : 0x%02x \n", rsp->ccode);
            intf->close(intf);
            return 0xFE;
        }
    }

    // If retries is disabled, don't resend IPMI messages on failure
    if (!retries) {
        intf->session->retry = -1;
    }

    //wait - scan GET UPGRADE STATUS
    scan_ret = scan_upgrade_status(intf, img_info.inaccessibility_timeout);

    if( scan_ret != 0 ) {
        return scan_ret;
    }

    //Upload firmware block
    // NOTE: We're consciously performing block_nb's roll over
    for(offset=0, block_nb=0; offset < action->firmware_length; block_nb++){
        data[0] = 0x00;
        data[1] = block_nb;
        for(i=0; i < DATA_PER_BLOCK && offset < action->firmware_length; i++, offset++){
            data[i+2] = byte[action->data_offset + offset];
        }

        printf("\r[INFO] \t {Upgrade in progress} \t\t                           ");
        //set_percent(((float)offset)/((float)action->firmware_length));
        printf("\r[INFO] \t {Upgrade in progress} \t\t %d / %d ", offset, action->firmware_length);
        fflush(stdout);

        scan_ret = 0xFF;

        rsp = send_ipmi_cmd(intf, 0x2c, 0x32, data, i+2);

        if (rsp->ccode != 0x00) {
            scan_upgrade_status(intf, img_info.inaccessibility_timeout);
        }
    }

    printf("\n");

    //FINISH_FIRMWARE_UPLOAD
    data[0] = 0x00;                                             //PICMG ID
    data[1] = component; //action-> components;              //Component (only one for upgrade action)
    data[2] = (unsigned char)(action->firmware_length & 0x000000FF);
    data[3] = (unsigned char)((action->firmware_length >> 8) & 0x000000FF);
    data[4] = (unsigned char)((action->firmware_length >> 16) & 0x000000FF);
    data[5] = (unsigned char)((action->firmware_length >> 24) & 0x000000FF);

    rsp = send_ipmi_cmd(intf, 0x2c, 0x33, data, 6);
    if(rsp == NULL){
        intf->close(intf);
        return 0xF9;
    }

    if(rsp->ccode != 0x00){ //Ignore size error for now
        printf("[INFO] \t {FINISH_FIRMWARE_UPLOAD} \t Completion Code : 0x%02x \n", rsp->ccode);
        intf->close(intf);
        return 0xF8;
    }

    /* Activate Firmware */
    printf("[INFO] \t {ACTIVATE_FIRMWARE_UPLOAD} \t Sending activation command \n");

    data[0] = 0x00;          //PICMG ID
    rsp = send_ipmi_cmd(intf, 0x2c, 0x35, data, 1);

    if(rsp == NULL){
        intf->close(intf);
        return 0xF7;
    }

    if(rsp->ccode == 0x00) {
        intf->close(intf);
        return 0x00;
    } else if (rsp->ccode == 0xD5) {
        printf("[INFO] \t {ACTIVATE_FIRMWARE_UPLOAD} \t The most recent firmware is already active \n");
    }

    intf->close(intf);
    return 0xF7;
}

unsigned char scan_upgrade_status(ipmi_intf * intf, unsigned long max_timeout)
{
    struct ipmi_rs *rsp;
    unsigned char ccode;
    unsigned long timeout;

    do{
        ccode = 0xFF;

        rsp = send_ipmi_cmd(intf, 0x2c, 0x34, NULL, 0);
        if(rsp == NULL) {
            intf->close(intf);
            return 0xFD;
        } else {
            if(rsp->ccode == 0x80 || rsp->ccode == 0xc3){
                usleep(10000);
            } else if(rsp->ccode == 0x00) {
                ccode = rsp->data[2];
                usleep(10000);
            } else {
                usleep(10000);
            }
        }
        timeout++;
    }while((ccode != 0x00));

    return 0x00;
}
