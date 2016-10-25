#ifndef __BATT_PARAM_H__
#define __BATT_PARAM_H__

/* battery status and charging information*/

struct battery_type{

		bool is_power_on_reset;

		s32 voltage_mV;
		s32 current_mA;
		s32 discharge_mA;
		s32 charge_counter_mAh;
		s32 temp_01c;
		s32 last_temp_01c;
		s32 id_ohm;
		s32 vref_mv;

		s32 voltage_adc;
		s32 current_adc;
		s32 discharge_adc;
		s32 charge_counter_adc;
		s32 temp_adc;
		s32 last_temp_adc;
		s32 id_adc;
		s32 vref_adc;

		s32 id_index;
		s32 charge_full_design_mAh;
		s32 charge_full_real_mAh;

		s32 temp_index;
		s32 temp_check_index;

		s32 KADC_01p;
		s32 RARC_01p;
		s32 pd_m;

		s32 software_charge_counter_mAms;
		s32 thermal_id;
};

struct protect_flags_type{

		bool is_charging_enable_available;
		bool is_charging_high_current_avaialble;
		bool is_charging_indicator_available;
		bool is_battery_dead;
#if 0
		bool is_battery_overtemp;
#endif
		bool is_fake_room_temp;
		int (*func_update_charging_protect_flag)(int, int, int, bool*, bool*);
};

/* ds2746 register definition*/

#define DS2746_STATUS_PORF  (1 << 6)	/* write to 0 as power-up sequence ready*/
#define DS2746_STATUS_SMOD  (1 << 5)	/* write to 0 to disable DS2746 sleep mode*/
#define DS2746_STATUS_NBEN  (1 << 4)	/* write to 0 to disable blanking of negative currents*/
#define DS2746_STATUS_AIN0  (1 << 0)
#define DS2746_STATUS_AIN1  (1 << 1)

/* function prototypes*/

void battery_capacity_update(struct battery_type *battery, int capacity_01p);
bool battery_param_update(struct battery_type *battery, struct protect_flags_type *flags);
u32 BAHW_MyGetMSecs(void);
void battery_param_init(struct battery_type *battery);

#endif /* __BATT_PARAM_H__*/
