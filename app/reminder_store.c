#include "reminder_store.h"
#include "ds1302.h"

/* DS1302 后备 RAM：写命令 0xC0+2n，读命令 0xC1+2n（n=0~30） */
#define RAM_W(n)       ((uint8_t)(0xC0U + 2U * (n)))
#define RAM_R(n)       ((uint8_t)(0xC1U + 2U * (n)))

#define RAM_MAGIC      0xA6U
#define IDX_MAGIC      0U
#define IDX_AHOUR      1U
#define IDX_AMIN       2U
#define IDX_AMODE      3U
#define IDX_FOCUS      4U
#define IDX_CHECK      5U

static uint8_t Checksum(const ReminderSettings_t *s)
{
    return (uint8_t)(s->alarmHour ^ s->alarmMinute ^ s->alarmMode ^ s->focusMinutes ^ 0x5AU);
}

int ReminderStore_Load(ReminderSettings_t *s)
{
    ReminderSettings_t t;

    if (s == NULL) return -1;
    if (ds1302_read_rig(RAM_R(IDX_MAGIC)) != RAM_MAGIC) return -1;

    t.alarmHour    = ds1302_read_rig(RAM_R(IDX_AHOUR));
    t.alarmMinute  = ds1302_read_rig(RAM_R(IDX_AMIN));
    t.alarmMode    = ds1302_read_rig(RAM_R(IDX_AMODE));
    t.focusMinutes = ds1302_read_rig(RAM_R(IDX_FOCUS));

    if (ds1302_read_rig(RAM_R(IDX_CHECK)) != Checksum(&t)) return -1;
    if (t.alarmHour > 23 || t.alarmMinute > 59 || t.alarmMode > 3 ||
        t.focusMinutes < 1 || t.focusMinutes > 99)
        return -1;

    *s = t;
    return 0;
}

void ReminderStore_Save(const ReminderSettings_t *s)
{
    if (s == NULL) return;

    ds1302_wirte_rig(0x8E, 0x00);                         /* 关闭写保护 */
    ds1302_wirte_rig(RAM_W(IDX_AHOUR), s->alarmHour);
    ds1302_wirte_rig(RAM_W(IDX_AMIN),  s->alarmMinute);
    ds1302_wirte_rig(RAM_W(IDX_AMODE), s->alarmMode);
    ds1302_wirte_rig(RAM_W(IDX_FOCUS), s->focusMinutes);
    ds1302_wirte_rig(RAM_W(IDX_CHECK), Checksum(s));
    ds1302_wirte_rig(RAM_W(IDX_MAGIC), RAM_MAGIC);        /* 最后写魔数，保证数据完整 */
    ds1302_wirte_rig(0x8E, 0x80);                         /* 恢复写保护 */
}
