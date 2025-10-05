#include <lunar/asm/wrap.h>
#include <lunar/core/timekeeper.h>
#include <lunar/core/spinlock.h>
#include <lunar/core/io.h>
#include <lunar/mm/heap.h>

static inline u8 cmos_read(u8 reg) {
	outb(0x70, reg);
	return inb(0x71);
}

enum rtc_regs {
	RTC_REG_SECOND = 0x00,
	RTC_REG_MINUTE = 0x02,
	RTC_REG_HOUR = 0x04,
	RTC_REG_DAY = 0x07,
	RTC_REG_MONTH = 0x08,
	RTC_REG_YEAR = 0x09,
	RTC_REG_CENTURY = 0x32,
	RTC_REG_STATUS_A = 0x0A,
	RTC_REG_STATUS_B = 0x0B
};

enum rtc_flags {
	RTC_FLAG_A_UIP = (1 << 7),
	RTC_FLAG_B_24HR = (1 << 1)
};

static inline bool rtc_updating(void) {
	return !!(cmos_read(RTC_REG_STATUS_A) & RTC_FLAG_A_UIP);
}

static inline u8 bcd_to_bin(u8 val) {
	return (val & 0x0F) + ((val >> 4) * 10);
}

static SPINLOCK_DEFINE(rtc_lock);

static void __rtc_read_time(u8* sec, u8* min, u8* hour, u8* day, u8* month, u16* year) {
	while (rtc_updating())
		cpu_relax();

	*sec = cmos_read(RTC_REG_SECOND);
	*min = cmos_read(RTC_REG_MINUTE);
	*hour = cmos_read(RTC_REG_HOUR);
	*day = cmos_read(RTC_REG_DAY);
	*month = cmos_read(RTC_REG_MONTH);

	u8 year_low = cmos_read(RTC_REG_YEAR);
	u8 cent = cmos_read(RTC_REG_CENTURY);

	u8 reg_b = cmos_read(RTC_REG_STATUS_B);
	if (!(reg_b & 0x04)) {
		*sec = bcd_to_bin(*sec);
		*min = bcd_to_bin(*min);
		*hour = bcd_to_bin(*hour);
		*day = bcd_to_bin(*day);
		*month = bcd_to_bin(*month);
		year_low = bcd_to_bin(year_low);
		cent = bcd_to_bin(cent);
	}

	*year = (cent * 100) + year_low;
	if (!(reg_b & RTC_FLAG_B_24HR) && (*hour & 0x80))
		*hour = ((*hour & 0x7F) + 12) % 24;
}

static void rtc_read_time(u8* sec, u8* min, u8* hour, u8* day, u8* month, u16* year) {
	irqflags_t irq_flags;
	spinlock_lock_irq_save(&rtc_lock, &irq_flags);

	/* 
	 * There is a small race condition where the RTC may update while reading from the registers. 
	 * If the times aren't equal, then retry.
	 */
	u8 last_sec, last_min, last_hour, last_day, last_month;
	u16 last_year;
	do {
		__rtc_read_time(&last_sec, &last_min, &last_hour, &last_day, &last_month, &last_year);
		__rtc_read_time(sec, min, hour, day, month, year);
	} while (*sec != last_sec || *min != last_min || *hour != last_hour || *day != last_day || *month != last_month || *year != last_year);

	spinlock_unlock_irq_restore(&rtc_lock, &irq_flags);
}

static inline bool is_leap(u16 y) {
	return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static struct timespec wc_get(struct timekeeper_source* source) {
	(void)source;

	u8 sec, min, hour, day, month;
	u16 year;
	rtc_read_time(&sec, &min, &hour, &day, &month, &year);

	if (year < 1970)
		year += 2000;

	static const int mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

	time_t days = 0;
	for (u16 y = 1970; y < year; y++)
		days += is_leap(y) ? 366 : 365;
	for (int m = 1; m < month; m++) {
		days += mdays[m - 1];
		if (m == 2 && is_leap(year))
			days++;
	}

	day += day - 1;
	return (struct timespec){ .tv_sec = days * 86400 + hour * 3600 + min * 60 + sec, .tv_nsec = 0 };
}

static int init(struct timekeeper_source** out) {
	struct timekeeper_source* s = kzalloc(sizeof(*s), MM_ZONE_NORMAL);
	if (!s)
		return -ENOMEM;

	s->wc_get = wc_get;
	*out = s;

	return 0;
}

static struct timekeeper __timekeeper rtc_timekeeper = {
	.name = "rtc",
	.type = TIMEKEEPER_WALLCLOCK,
	.init = init,
	.early = false,
	.rating = 1 /* No idea how this should be rated */
};
