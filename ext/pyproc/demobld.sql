-- good, old scott/tiger demo schema, converted for sqlite...
--
drop table if exists emp;
drop table if exists dept;
drop table if exists bonus;
drop table if exists salgrade;
drop table if exists dummy;

create table emp
(empno number(4) not null,
ename varchar2(10),
job varchar2(9),
mgr number(4),
hiredate date,
sal number(7, 2),
comm number(7, 2),
deptno number(2));

insert into emp values
(7369, 'smith', 'clerk', 7902, '1980-12-17', 800, null, 20);
insert into emp values
(7499, 'allen', 'salesman', 7698, '1981-02-20', 1600, 300, 30);
insert into emp values
(7521, 'ward', 'salesman', 7698, '1981-02-22', 1250, 500, 30);
insert into emp values
(7566, 'jones', 'manager', 7839, '1981-04-02', 2975, null, 20);
insert into emp values
(7654, 'martin', 'salesman', 7698, '1981-10-28', 1250, 1400, 30);
insert into emp values
(7698, 'blake', 'manager', 7839, '1981-05-01', 2850, null, 30);
insert into emp values
(7782, 'clark', 'manager', 7839, '1981-06-09', 2450, null, 10);
insert into emp values
(7788, 'scott', 'analyst', 7566, '1982-12-09', 3000, null, 20);
insert into emp values
(7839, 'king', 'president', null, '1981-11-17', 5000, null, 10);
insert into emp values
(7844, 'turner', 'salesman', 7698, '1981-10-08', 1500, 0, 30);
insert into emp values
(7876, 'adams', 'clerk', 7788, '1983-01-12', 1100, null, 20);
insert into emp values
(7900, 'james', 'clerk', 7698, '1981-12-03', 950, null, 30);
insert into emp values
(7902, 'ford', 'analyst', 7566, '1981-12-03', 3000, null, 20);
insert into emp values
(7934, 'miller', 'clerk', 7782, '1982-01-23', 1300, null, 10);

create table dept
(deptno number(2),
dname varchar2(14),
loc varchar2(13) );

insert into dept values (10, 'accounting', 'new york');
insert into dept values (20, 'research', 'dallas');
insert into dept values (30, 'sales', 'chicago');
insert into dept values (40, 'operations', 'boston');

create table bonus
(ename varchar2(10),
job varchar2(9),
sal number,
comm number);

create table salgrade
(grade number,
losal number,
hisal number);

insert into salgrade values (1, 700, 1200);
insert into salgrade values (2, 1201, 1400);
insert into salgrade values (3, 1401, 2000);
insert into salgrade values (4, 2001, 3000);
insert into salgrade values (5, 3001, 9999);

create table dummy
(dummy number);

insert into dummy values (0);
